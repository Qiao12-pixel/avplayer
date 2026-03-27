//
// Created by Joe on 26-3-23.
//

#include "DeMuxer.h"
#include <iostream>

namespace av {
    IDeMuxer *IDeMuxer::Create() {
        return new DeMuxer();
    }
    DeMuxer::DeMuxer() {
        m_audioStream.pipelineReleaseCallback = std::make_shared<std::function<void()>>([&]() {
           ReleaseAudioPipelineResource();
        });
        m_videoStream.pipelineReleaseCallback = std::make_shared<std::function<void()>>([&]() {
            ReleaseVideoPipelineResource();
        });
        m_thread = std::thread(&DeMuxer::ThreadLoop, this);
    }
    DeMuxer::~DeMuxer() {
        Stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
        std::lock_guard<std::mutex> lock(m_fmtCtxMutex);
        if (m_fmtCtx) {
            avformat_close_input(&m_fmtCtx);
        }
    }
    void DeMuxer::SetListener(Listener *listener) {
        std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
        m_listener = listener;
    }
    bool DeMuxer::Open(const std::string &url) {
        //打开文件，找流并向上层推送视频流
        std::lock_guard<std::mutex> lock(m_fmtCtxMutex);
        if (avformat_open_input(&m_fmtCtx, url.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "failed open intput" << std::endl;
            return false;
        }
        if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
            std::cerr << "failed find stream information" << std::endl;
            return false;
        }
        for (unsigned int i = 0; i < m_fmtCtx->nb_streams; ++i) {
            if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                m_audioStream.streamIndex = i;
                std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
                if (m_listener) {
                    m_listener->OnNotifyAudioStream(m_fmtCtx->streams[i]);
                } else {
                    std::cerr << "未能向上层音频解码器发送音频流信息" << std::endl;
                }
            } else if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                m_videoStream.streamIndex = i;
                std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
                if (m_listener) {
                    m_listener->OnNotifyVideoStream(m_fmtCtx->streams[i]);
                } else {
                    std::cerr << "未能向上层发送视频解码器视频流信息" << std::endl;
                }
            }
        }
        return true;
    }
    void DeMuxer::Start() {
        m_paused = false;
        m_notifier.Notify();
    }
    void DeMuxer::Pause() {
        m_paused = true;
    }
    void DeMuxer::SeekTo(float position) {
        m_seekProgress = position;
        m_seek = true;
        m_notifier.Notify();
    }
    void DeMuxer::Stop() {
        m_abort = true;
        m_notifier.Notify();
    }
    float DeMuxer::GetDuration() {
        std::lock_guard<std::mutex> lock(m_fmtCtxMutex);
        if (!m_fmtCtx) {
            return 0;
        }
        return m_fmtCtx->duration / static_cast<float>(AV_TIME_BASE);//获取视频总时长
    }
    void DeMuxer::ThreadLoop() {
        for (;;) {
            m_notifier.Wait(100);
            if (m_abort) {
                break;
            } if (m_seek) {
                ProcessSeek();
            } if (!m_paused && (m_audioStream.pipelineResourceCount > 0 || m_videoStream.pipelineResourceCount > 0)) {
                if (!ReadAndSendPacket()) {
                    break;
                }
            }
        }
    }
    void DeMuxer::ProcessSeek() {
        std::lock_guard<std::mutex> lock(m_fmtCtxMutex);
        if (!m_fmtCtx) {
            return;
        }
        int64_t timeStamp = static_cast<int64_t>(m_seekProgress * m_fmtCtx->duration);
        if (av_seek_frame(m_fmtCtx, -1, timeStamp, AVSEEK_FLAG_BACKWARD) < 0) {
            std::cerr << "Seek failed " << std::endl;
        }
        {
            std::lock_guard<std::recursive_mutex> listenerLock(m_listenerMutex);
            auto audioPacket = std::make_shared<IAVPacket>(nullptr);
            audioPacket->flags |= AVFrameFlag::KFlush;//停，发刷新包，前面的包不要了【这也是拖动进度条播放逻辑】
            m_listener->OnNotifyAudioPacket(audioPacket);

            auto videoPacket = std::make_shared<IAVPacket>(nullptr);
            videoPacket->flags |= AVFrameFlag::KFlush;
            m_listener->OnNotifyVideoPacket(videoPacket);
        }
        m_seekProgress = -1.0f;
        m_seek = false;
    }

    bool DeMuxer::ReadAndSendPacket() {
        std::lock_guard<std::mutex> lock(m_fmtCtxMutex);
        if (m_fmtCtx == nullptr) {
            m_paused = false;
            return true;
        }
        AVPacket packet;
        auto ret = av_read_frame(m_fmtCtx, &packet);
        if (ret >= 0) {
            std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
            if (m_listener) {
                if (packet.stream_index == m_audioStream.streamIndex) {
                    --m_audioStream.pipelineResourceCount;
                    /*
                     * IAVPacket 构造函数里，由于传进去了一个真实的 AVPacket，
                     * 它顺手就把 FFmpeg 底层的 flags（比如关键帧标记 1）赋值给了你的自定义 flags，
                     * 从而导致了我们上一轮分析的“维度碰撞”（关键帧被误认为是 KFlush 刷新帧）！
                     */
                    auto sharedPacket = std::make_shared<IAVPacket>(av_packet_clone(&packet));
                    // 👇 核心修复：强制归零业务标记！斩断关键帧被误认为 Flush 的 Bug！
                    sharedPacket->flags = 0;
                    sharedPacket->releaseCallback = m_audioStream.pipelineReleaseCallback;
                    m_listener->OnNotifyAudioPacket(sharedPacket);
                } else if (packet.stream_index == m_videoStream.streamIndex) {
                    --m_videoStream.pipelineResourceCount;
                    auto sharedPacket = std::make_shared<IAVPacket>(av_packet_clone(&packet));
                    // 👇 核心修复：强制归零业务标记！斩断关键帧被误认为 Flush 的 Bug！
                    sharedPacket->flags = 0;
                    sharedPacket->releaseCallback = m_videoStream.pipelineReleaseCallback;
                    m_listener->OnNotifyVideoPacket(sharedPacket);
                }
            }
            av_packet_unref(&packet);
        } else if (ret == AVERROR_EOF) {
            m_paused = true;
            std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
            if (m_listener) {
                m_listener->OnDeMuxEOF();
            }
        } else {
            return false;
        }
        return true;
    }

    void DeMuxer::ReleaseAudioPipelineResource() {
        ++m_audioStream.pipelineResourceCount;
        m_notifier.Notify();
    }
    void DeMuxer::ReleaseVideoPipelineResource() {
        ++m_videoStream.pipelineResourceCount;
        m_notifier.Notify();
    }
}