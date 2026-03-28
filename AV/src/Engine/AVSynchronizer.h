//
// Created by Joe on 26-3-26.
//

#ifndef AVSYNCHRONIZER_H
#define AVSYNCHRONIZER_H

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

#include "../Define/IAudioSamples.h"
#include "../Define/IVideoFrame.h"
#include "AudioRender.h"

class PlayerWidget;

namespace av {
    class AVSynchronizer {
    public:
        AVSynchronizer();
        ~AVSynchronizer();

        bool Init(unsigned int audioChannels = 2, unsigned int sampleRate = 44100);
        void SetVideoOutput(PlayerWidget* videoOutput);
        void PushAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples);
        void PushVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame);

        void Tick();
        void Stop();

        double GetMasterClock() const;

    private:
        std::shared_ptr<IVideoFrame> TakeReadyVideoFrame(double masterClock, bool useAudioClock);
        void ResetVideoClockLocked();

        AudioRender m_audioRender;
        PlayerWidget* m_videoOutput{nullptr};

        mutable std::mutex m_videoQueueMutex;
        std::deque<std::shared_ptr<IVideoFrame>> m_videoQueue;
        std::chrono::steady_clock::time_point m_videoClockStart{};
        double m_videoClockBasePts{-1.0};
        bool m_audioClockEnabled{false};
    };
}

#endif //AVSYNCHRONIZER_H
