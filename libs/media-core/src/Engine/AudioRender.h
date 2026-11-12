//
// Created by Joe on 26-3-27.
//

#ifndef AUDIORENDER_H
#define AUDIORENDER_H

#include <QAudioFormat>
#include <QAudioSink>

#include <atomic>
#include <memory>
#include <mutex>

#include "../Define/IAudioSamples.h"

namespace av {
    class AudioRender {
    public:
        AudioRender();
        ~AudioRender();

        bool Init(unsigned int channels, unsigned int sampleRate);
        void Reset();
        void PushSamples(const std::shared_ptr<IAudioSamples>& samples);
        void Flush();
        void Stop();

        double GetClockInSeconds() const;
        bool HasValidClock() const;

    private:
        void ResetClockState();

        std::unique_ptr<QAudioSink> m_audioSink;
        QIODevice* m_outputDevice{nullptr};
        QAudioFormat m_format;

        mutable std::mutex m_clockMutex;
        double m_clockBasePts{-1.0};
        std::atomic<bool> m_started{false};
    };
}

#endif //AUDIORENDER_H
