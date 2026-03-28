//
// Created by Codex on 26-3-28.
//

#ifndef RECORDSESSION_H
#define RECORDSESSION_H

#include <mutex>
#include <string>

#include "IRecordSession.h"
#include "IRecorder.h"

namespace av {
    class RecordSession final : public IRecordSession, public IRecorder::Listener {
    public:
        RecordSession();
        ~RecordSession() override;

        void SetListener(IRecordSession::Listener* listener) override;
        bool Start(const RecorderConfig& config) override;
        bool StopAndSave(const std::string& outputFilePath) override;
        void Abort() override;
        bool IsRecording() const override;
        std::string GetTempFilePath() const override;
        std::string BuildDefaultOutputPath(const std::string& directory) const override;
        void WriteAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples) override;
        void WriteVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame) override;

        void OnRecorderStarted(const std::string& filePath) override;
        void OnRecorderStopped(const std::string& filePath) override;
        void OnRecorderError(int code, const std::string& message) override;

    private:
        std::string CreateTempFilePath() const;
        void NotifyErrorLocked(const std::string& message) const;

        mutable std::mutex m_mutex;
        IRecordSession::Listener* m_listener{nullptr};
        std::unique_ptr<IRecorder> m_recorder;
        std::string m_tempFilePath;
    };
}

#endif //RECORDSESSION_H
