//
// Created by Joe on 26-3-24.
//

#include "MiniDecoder.h"

/*
 * MiniDecoder是测试解复用的，不参与总体框架运行
 */
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
namespace av {
    MiniDecoder::~MiniDecoder() {
        if (m_videoCodecCtx) {
            avcodec_free_context(&m_videoCodecCtx);
        } if (m_audioCodecCtx) {
            avcodec_free_context(&m_audioCodecCtx);
        }
    }
    void MiniDecoder::ReleasePacket(std::shared_ptr<IAVPacket> &packet) {
        if (auto lockedCallback = packet->releaseCallback.lock()) {
            (*lockedCallback)();
        }
    }

    void MiniDecoder::OnNotifyAudioStream(struct AVStream *stream) {
        std::cout << "[MiniDecoder]收到音频流，音频流：index = " << stream->index << std::endl;
    }
    void MiniDecoder::OnNotifyVideoStream(struct AVStream *stream) {
        //std::cout << "[MiniDecoder]视频流：index = " << stream->index << std::endl;
        std::cout << "[MiniDecoder]收到视频流， 视频流: index = " << stream->index << "准备初始化解码器" << std::endl;

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "[MiniDecoder]找不到相对应的视频解码器" << std::endl;
            return;
        }
        m_videoCodecCtx = avcodec_alloc_context3(codec);

        avcodec_parameters_to_context(m_videoCodecCtx, stream->codecpar);

        if (avcodec_open2(m_videoCodecCtx, codec, nullptr) < 0) {
            std::cerr << "[MiniDecoder] 打开视频解码器失败" << std::endl;
            return;
        }
        std::cout << "[MiniDecoder] 视频解码器初始化成功 宽： " << m_videoCodecCtx->width << "高: " << m_videoCodecCtx->height << std::endl;
    }
    void MiniDecoder::OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) {
        // 这里暂时不解码音频，直接释放资源避免阻塞
        ReleasePacket(packet);
        std::cout << "已收到packet，不进行音频解码" << std::endl;
    }
    void MiniDecoder::OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) {
        if (!m_videoCodecCtx) {
            ReleasePacket(packet);
        } if (packet->flags & AVFrameFlag::KFlush) {
            std::cout << "[MiniDecoder] 收到Flush包，清空解码器内部缓存" << std::endl;
            avcodec_flush_buffers(m_videoCodecCtx);
            ReleasePacket(packet);
            return;
        }
        int ret = avcodec_send_packet(m_videoCodecCtx, packet->avPacket);
        if (ret < 0) {
            std::cerr << "[MiniDecoder] 发送视频包失败" << std::endl;
            ReleasePacket(packet);
            return;
        }
        //没有问题，去解码
        AVFrame* frame = av_frame_alloc();
        while (ret >= 0) {
            ret = avcodec_receive_frame(m_videoCodecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;//需要更多的数据，或到了文件末尾
            } else if (ret < 0 ) {
                std::cerr << "[MiniDecoder] 接受视频帧失败" << std::endl;
                break;
            }
            std::cout << "[MiniDecoder] 成功解码一帧视频，格式 " << frame->format << "宽：" << frame->width << "高: " << frame->height << "PTS：" << frame->pts << std::endl;

        }
        av_frame_free(&frame);
        ReleasePacket(packet);
    }





}