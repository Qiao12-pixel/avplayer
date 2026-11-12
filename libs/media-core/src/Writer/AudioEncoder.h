//
// Created by Codex on 26-3-28.
//

#ifndef AUDIOENCODER_H
#define AUDIOENCODER_H

#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

#include "IRecorder.h"
#include "Interface/IAudioEncoder.h"

namespace av {
    class AudioEncoder final : public IAudioEncoder {
    public:
        AudioEncoder();
        ~AudioEncoder() override;

        void SetListener(Listener* listener) override;
        bool Open(const RecorderConfig& config, bool globalHeader) override;
        void Encode(const std::shared_ptr<IAudioSamples>& audioSamples, int64_t recordingStartUs) override;
        void Flush() override;
        void Close() override;
        AVCodecContext* GetCodecContext() const override;

    private:
        void DrainAudioFifo(bool flushAll);
        bool EmitPackets(AVFrame* frame);
        int64_t GetTimestampUs(int64_t pts, int32_t timebaseNum, int32_t timebaseDen) const;
        void CleanupLocked();
        void ResetState();
        void LogError(const char* prefix, int errorCode) const;

        mutable std::mutex m_mutex;
        Listener* m_listener{nullptr};
        AVCodecContext* m_codecContext{nullptr};
        SwrContext* m_swrContext{nullptr};
        AVAudioFifo* m_audioFifo{nullptr};
        int64_t m_nextAudioPts{0};
        bool m_audioPtsInitialized{false};
    };
}

#endif //AUDIOENCODER_H
