//
// Created by Joe on 26-3-25.
//

#include "VideoDecoder.h"

#include <iostream>
#include <thread>
extern "C" {
#include <libavutil/imgutils.h>
}
namespace av {
    IVideoDecoder *IVideoDecoder::Create() {
        return new VideoDecoder();
    }
    VideoDecoder::VideoDecoder() {
        m_pipelineReleaseCallback = std::make_shared<std::function<void()>>([&]() {
           ReleaseVideoPipelineResource();
        });
        m_thread = std::thread(&VideoDecoder::ThreadLoop, this);
    }
    VideoDecoder::~VideoDecoder() {
        Stop();
        std::lock_guard<std::mutex> lock(m_codecContexMutex);
        CLeanupContext();
    }
    void VideoDecoder::SetListener(Listener *listener) {
        std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
        m_listener = listener;
    }
    void VideoDecoder::SetStream(struct AVStream *stream) {
        if (!stream) {
            std::cerr << "视频解码器没有收到流信息" << std::endl;
        }
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            m_packetQueue.clear();
        }
        std::lock_guard<std::mutex> lock(m_codecContexMutex);
        CLeanupContext();

        //找解码器
        AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "视频解码器初始化失败" << std::endl;
            return;
        }
        m_codecContex = avcodec_alloc_context3(codec);
        if (!m_codecContex) {
            std::cerr << "视频解码器上下文分配失败" << std::endl;
        }
        if (avcodec_parameters_to_context(m_codecContex, stream->codecpar) < 0) {
            std::cerr << "不能绑定视频解码器参数" << std::endl;
            avcodec_free_context(&m_codecContex);
            return;
        }
        if (avcodec_open2(m_codecContex, codec, nullptr) < 0) {
            std::cerr << "不能打开解码器" << std::endl;
            avcodec_free_context(&m_codecContex);
            return;
        }
        m_timeBase = stream->time_base;
    }
    void VideoDecoder::Decode(std::shared_ptr<IAVPacket> packet) {
        if (!packet) {
            std::cerr << "未接受到packet" << std::endl;
        }
        std::lock_guard<std::mutex> lock(m_packetQueueMutex);
        if (packet->flags & AVFrameFlag::KFlush) {
            m_packetQueue.clear();
        }
        m_packetQueue.push_back(packet);
        m_notifier.Notify();//通知threadLoop线程，上一步的packet队列，现在线程工作[DeMuxer线程送入的packet]
    }
    void VideoDecoder::Start() {
        m_paused = false;
        m_notifier.Notify();
    }
    void VideoDecoder::Pause() {
        m_paused = true;
    }
    void VideoDecoder::Stop() {
        m_abort = true;
        m_notifier.Notify();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    int VideoDecoder::GetVideoWidth() {
        std::lock_guard<std::mutex> lock(m_codecContexMutex);
        return m_codecContex ? m_codecContex->width : 0;
    }
    int VideoDecoder::GetVideoHeight() {
        std::lock_guard<std::mutex> lock(m_codecContexMutex);
        return m_codecContex ? m_codecContex->height : 0;
    }
    void VideoDecoder::ThreadLoop() {
        for (;;) {
            m_notifier.Wait(100);//释放锁，等待唤醒
            if (m_abort) {
                break;
            }
            checkFlushPacket();
            if (!m_paused && m_pipelineResourceCount > 0) {
                DecodeAVPacket();
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            m_packetQueue.clear();
        }
    }
    void VideoDecoder::checkFlushPacket() {
        // std::lock_guard<std::mutex> lock(m_packetQueueMutex);
        // if (m_packetQueue.empty()) {
        //     return;
        // }
        // auto packet = m_packetQueue.front();
        // if (packet->flags & AVFrameFlag::KFlush) {
        //     m_packetQueue.pop_front();
        //     avcodec_flush_buffers(m_codecContex);
        //
        //     auto videoFrame =std::make_shared<IVideoFrame>();
        //     videoFrame->flags |= AVFrameFlag::KFlush;//标记该帧为刷新帧，不携带数据
        //     std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
        //     if (m_listener) {
        //         m_listener->OnNotifyVideoFrame(videoFrame);
        //     }
        // }
        /*
        * 用 m_packetQueueMutex 锁住了整个过程。
        * 但在清空队列后，调用了 avcodec_flush_buffers(m_codecContex);。
        * 因为 m_codecContex 可能会被外部的 SetStream 修改，
        * 所以操作它时必须加 m_codecContexMutex 锁。【如果同时握着两把锁，在复杂情况下极易引发死锁（Deadlock）】
        * 解决方案：快进快出，尽早释放锁
         */
        bool needFlush = false;
        //只锁队列，检查并取出包，然后立即释放
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            if (!m_packetQueue.empty()) {
                auto packet = m_packetQueue.front();
                if (packet->flags & AVFrameFlag::KFlush) {
                    m_packetQueue.pop_front();
                    needFlush = true;//标记刷新
                }
            }
        }//队列解锁，后面DeMuxer可以加入packet

        //给解码器枷锁
        if (needFlush) {
            std::lock_guard<std::mutex> codecLock(m_codecContexMutex);
            if (m_codecContex) {
                avcodec_flush_buffers(m_codecContex);//执行时间长，但是不能阻止DeMuxer进行队列的入队
            }
            auto videoFrame = std::make_shared<IVideoFrame>();
            videoFrame->flags |= AVFrameFlag::KFlush;
            std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
            if (m_listener) {
                m_listener->OnNotifyVideoFrame(videoFrame);
            }
        }

    }
    //收到packet包，开始解码视频帧
    void VideoDecoder::DecodeAVPacket() {
        std::shared_ptr<IAVPacket> packet;
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            if (m_packetQueue.empty()) {
                return;
            }
            packet = m_packetQueue.front();
            m_packetQueue.pop_front();
        }
        std::lock_guard<std::mutex> lock(m_codecContexMutex);
        if (packet->avPacket && avcodec_send_packet(m_codecContex, packet->avPacket) < 0) {
            std::cerr << "failed sending Video packet to decoding" << std::endl;
            return;
        }
        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            std::cerr << "failed allocate video frame" << std::endl;
            return;
        }
        while (true) {
            int ret = avcodec_receive_frame(m_codecContex, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "failed during decode" << std::endl;
                av_frame_free(&frame);
                return;
            }
            if (!m_swsContext) {
                m_swsContext = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!m_swsContext) {
                    std::cerr << "failed init SWS context" << std::endl;
                    av_frame_free(&frame);
                    return;
                }
            }
            AVFrame* rgbFrame = av_frame_alloc();
            if (!rgbFrame) {
                std::cerr << "failed allocate rbgframe" << std::endl;
                av_frame_free(&rgbFrame);
                return;
            }

            int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame->width, frame->height, 1);
            std::shared_ptr<uint8_t> buffer(new uint8_t[numBytes], std::default_delete<uint8_t[]>());
            if (av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer.get(), AV_PIX_FMT_RGBA, frame->width, frame->height, 1) < 0) {
                av_frame_free(&rgbFrame);
                av_frame_free(&frame);
                return;
            }
            //开始转换
            sws_scale(m_swsContext, frame->data, frame->linesize, 0, frame->height, rgbFrame->data, rgbFrame->linesize);
            //复制信息
            auto videoFrame = std::make_shared<IVideoFrame>();
            videoFrame->width = frame->width;
            videoFrame->height = frame->height;
            videoFrame->data = std::move(buffer);
            videoFrame->pts = frame->pts;
            videoFrame->duration = frame->pkt_duration;
            videoFrame->timebaseNum = m_timeBase.num;
            videoFrame->timebaseDen = m_timeBase.den;
            videoFrame->releaseCallback = m_pipelineReleaseCallback;
            av_frame_free(&rgbFrame);
            --m_pipelineResourceCount;
            {
                std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
                if (m_listener) {
                    m_listener->OnNotifyVideoFrame(videoFrame);
                }
            }
        }
        av_frame_free(&frame);
    }
    void VideoDecoder::ReleaseVideoPipelineResource() {
        ++m_pipelineResourceCount;
        m_notifier.Notify();
    }
    void VideoDecoder::CLeanupContext() {
        if (m_codecContex) {
            avcodec_free_context(&m_codecContex);
        }
        if (m_swsContext) {
            sws_freeContext(m_swsContext);
        }
    }
}
