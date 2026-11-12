//
// Created by Codex on 26-3-28.
//

#ifndef IRECORDSESSION_H
#define IRECORDSESSION_H

#include <memory>
#include <string>

namespace av {
    struct IAudioSamples;
    struct IVideoFrame;
    struct RecorderConfig;

    class IRecordSession {
    public:
        struct Listener {
            virtual void OnRecordSessionStarted(const std::string& tempFilePath) = 0;
            virtual void OnRecordSessionSaved(const std::string& outputFilePath) = 0;
            virtual void OnRecordSessionAborted() = 0;
            virtual void OnRecordSessionError(const std::string& message) = 0;
            virtual ~Listener() = default;
        };

        virtual ~IRecordSession() = default;

        static IRecordSession* Create();

        virtual void SetListener(Listener* listener) = 0;
        virtual bool Start(const RecorderConfig& config) = 0;
        virtual bool StopAndSave(const std::string& outputFilePath) = 0;
        virtual void Abort() = 0;
        virtual bool IsRecording() const = 0;
        virtual std::string GetTempFilePath() const = 0;
        virtual std::string BuildDefaultOutputPath(const std::string& directory) const = 0;
        virtual void WriteAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples) = 0;
        virtual void WriteVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame) = 0;
    };
}

#endif //IRECORDSESSION_H
