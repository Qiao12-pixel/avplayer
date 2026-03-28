//
// Created by Codex on 26-3-28.
//

#ifndef RECORDER_H
#define RECORDER_H

#include <mutex>
#include <string>

#include "../Define/IAudioSamples.h"
#include "../Define/IVideoFrame.h"
#include "IRecorder.h"
#include "IMuxer.h"
#include "Interface/IAudioEncoder.h"
#include "Interface/IVideoEncoder.h"

namespace av {
    class Recorder final : public IRecorder,
                           public IMuxer::Listener,
                           public IAudioEncoder::Listener,
                           public IVideoEncoder::Listener {
    public:
        Recorder();
        ~Recorder() override;

        void SetListener(IRecorder::Listener* listener) override;
        bool Start(const std::string& filePath, const RecorderConfig& config) override;
        void Stop() override;
        bool IsRecording() const override;
        void WriteAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples) override;
        void WriteVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame) override;
        void OnMuxerOpen(const std::string& filePath) override;
        void OnMuxerClose(const std::string& filePath) override;
        void OnMuxerError(int code, const std::string& message) override;
        void OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) override;
        void OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) override;

    private:
        bool InitializeOutput(const std::string& filePath, const RecorderConfig& config);
        int64_t GetTimestampUs(int64_t pts, int32_t timebaseNum, int32_t timebaseDen) const;
        void Cleanup();
        void ResetState();

        mutable std::mutex m_mutex;
        IRecorder::Listener* m_listener{nullptr};
        bool m_recording{false};
        std::string m_filePath;
        RecorderConfig m_config;
        std::unique_ptr<IAudioEncoder> m_audioEncoder;
        std::unique_ptr<IVideoEncoder> m_videoEncoder;
        std::unique_ptr<IMuxer> m_muxer;
        bool m_useGlobalHeader{true};
        int64_t m_recordingStartUs{AV_NOPTS_VALUE};
    };
}

#endif //RECORDER_H
