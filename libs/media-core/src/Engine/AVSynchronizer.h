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
#include "IAudioSamplesSink.h"
#include "IFrameSink.h"

namespace av {
    class AVSynchronizer {
    public:
        AVSynchronizer();
        ~AVSynchronizer();

        bool Init(unsigned int audioChannels = 2, unsigned int sampleRate = 44100);
        void SetVideoOutput(IFrameSink* videoOutput);
        // When set, decoded audio is delivered here instead of the local Qt
        // audio device (used by media_server's WebRTC audio track).
        void SetAudioOutput(IAudioSamplesSink* audioOutput);
        void PushAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples);
        void PushVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame);

        void Tick();
        void Stop();
        void Shutdown();

        double GetMasterClock() const;
        double GetAudioClock() const;

    private:
        std::shared_ptr<IVideoFrame> TakeReadyVideoFrame(double masterClock, bool useAudioClock);
        void ResetVideoClockLocked();

        AudioRender m_audioRender;
        IFrameSink* m_videoOutput{nullptr};
        IAudioSamplesSink* m_audioOutput{nullptr};

        mutable std::mutex m_videoQueueMutex;
        std::deque<std::shared_ptr<IVideoFrame>> m_videoQueue;
        std::chrono::steady_clock::time_point m_videoClockStart{};
        double m_videoClockBasePts{-1.0};
        bool m_audioClockEnabled{false};
    };
}

#endif //AVSYNCHRONIZER_H
