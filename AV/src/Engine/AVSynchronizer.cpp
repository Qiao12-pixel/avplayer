//
// Created by Joe on 26-3-26.
//

#include "AVSynchronizer.h"

#include <iostream>

#include "../../../qt/UI/PlayerWidget.h"

namespace av {
    namespace {
        constexpr size_t kMaxVideoQueueSize = 60;
        constexpr double kVideoLeadToleranceSeconds = 0.010;
        constexpr double kVideoDropToleranceSeconds = -0.080;
    }

    AVSynchronizer::AVSynchronizer() = default;

    AVSynchronizer::~AVSynchronizer() {
        Stop();
    }

    bool AVSynchronizer::Init(unsigned int audioChannels, unsigned int sampleRate) {
        m_audioClockEnabled = m_audioRender.Init(audioChannels, sampleRate);
        if (!m_audioClockEnabled) {
            std::cerr << "音频输出初始化失败，先使用视频时钟继续播放" << std::endl;
        }
        return true;
    }

    void AVSynchronizer::SetVideoOutput(PlayerWidget* videoOutput) {
        m_videoOutput = videoOutput;
    }

    void AVSynchronizer::PushAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples) {
        static int audioPushCount = 0;
        ++audioPushCount;
        if (audioPushCount <= 3 || audioPushCount % 200 == 0) {
            std::cout << "[AVSynchronizer] audio pushed #" << audioPushCount
                      << " ts=" << audioSamples->GetTimeStamp()
                      << " flags=" << audioSamples->flags << std::endl;
        }
        m_audioRender.PushSamples(audioSamples);
    }

    void AVSynchronizer::PushVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame) {
        if (!videoFrame) {
            return;
        }
        static int videoPushCount = 0;
        ++videoPushCount;
        if (videoPushCount <= 5 || videoPushCount % 120 == 0 || (videoFrame->flags & AVFrameFlag::KFlush)) {
            std::cout << "[AVSynchronizer] video queued #" << videoPushCount
                      << " ts=" << videoFrame->GetTimeStamp()
                      << " flags=" << videoFrame->flags
                      << " hasData=" << (videoFrame->data ? 1 : 0) << std::endl;
        }

        std::lock_guard<std::mutex> lock(m_videoQueueMutex);
        if (videoFrame->flags & AVFrameFlag::KFlush) {
            m_videoQueue.clear();
            ResetVideoClockLocked();
            return;
        }
        if (!videoFrame->data) {
            return;
        }

        if (m_videoQueue.size() >= kMaxVideoQueueSize) {
            m_videoQueue.pop_front();
        }
        m_videoQueue.push_back(videoFrame);

        if (!m_audioClockEnabled && m_videoClockBasePts < 0.0) {
            m_videoClockBasePts = videoFrame->GetTimeStamp();
            m_videoClockStart = std::chrono::steady_clock::now();
        }
    }

    void AVSynchronizer::Tick() {
        const double audioClock = m_audioRender.GetClockInSeconds();
        const bool useAudioClock = audioClock >= 0.0;
        const double masterClock = useAudioClock ? audioClock : GetMasterClock();

        auto readyFrame = TakeReadyVideoFrame(masterClock, useAudioClock);
        if (readyFrame && m_videoOutput) {
            static int dispatchCount = 0;
            ++dispatchCount;
            if (dispatchCount <= 5 || dispatchCount % 120 == 0) {
                std::cout << "[AVSynchronizer] dispatch frame #" << dispatchCount
                          << " frameTs=" << readyFrame->GetTimeStamp()
                          << " masterClock=" << masterClock
                          << " useAudioClock=" << (useAudioClock ? 1 : 0) << std::endl;
            }
            m_videoOutput->PushFrame(readyFrame);
        }
    }

    void AVSynchronizer::Stop() {
        m_audioRender.Reset();
        {
            std::lock_guard<std::mutex> lock(m_videoQueueMutex);
            m_videoQueue.clear();
            ResetVideoClockLocked();
        }
        if (m_videoOutput) {
            m_videoOutput->ClearFrame();
        }
    }

    double AVSynchronizer::GetMasterClock() const {
        const double audioClock = m_audioRender.GetClockInSeconds();
        if (audioClock >= 0.0) {
            return audioClock;
        }

        std::lock_guard<std::mutex> lock(m_videoQueueMutex);
        if (m_videoClockBasePts < 0.0) {
            return -1.0;
        }

        const auto elapsed = std::chrono::steady_clock::now() - m_videoClockStart;
        return m_videoClockBasePts + std::chrono::duration<double>(elapsed).count();
    }

    std::shared_ptr<IVideoFrame> AVSynchronizer::TakeReadyVideoFrame(double masterClock, bool useAudioClock) {
        std::lock_guard<std::mutex> lock(m_videoQueueMutex);
        while (!m_videoQueue.empty()) {
            auto frame = m_videoQueue.front();
            if (!frame || !frame->data) {
                m_videoQueue.pop_front();
                continue;
            }

            if (!useAudioClock && m_videoClockBasePts < 0.0) {
                m_videoClockBasePts = frame->GetTimeStamp();
                m_videoClockStart = std::chrono::steady_clock::now();
                masterClock = m_videoClockBasePts;
            }

            if (masterClock < 0.0) {
                m_videoQueue.pop_front();
                return frame;
            }

            const double delta = frame->GetTimeStamp() - masterClock;
            if (useAudioClock && delta < kVideoDropToleranceSeconds) {
                m_videoQueue.pop_front();
                continue;
            }

            if (delta <= kVideoLeadToleranceSeconds) {
                m_videoQueue.pop_front();
                return frame;
            }
            break;
        }

        return nullptr;
    }

    void AVSynchronizer::ResetVideoClockLocked() {
        m_videoClockBasePts = -1.0;
        m_videoClockStart = std::chrono::steady_clock::time_point{};
    }
}
