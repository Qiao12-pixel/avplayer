//
// Created by Codex on 26-3-28.
//

#ifndef IRECORDER_H
#define IRECORDER_H

#include <memory>
#include <string>

namespace av {
    struct IAudioSamples;
    struct IVideoFrame;

    struct RecorderConfig {
        int width{0};
        int height{0};
        int audioChannels{2};
        int audioSampleRate{44100};
    };

    class IRecorder {
    public:
        struct Listener {
            virtual void OnRecorderStarted(const std::string& filePath) = 0;
            virtual void OnRecorderStopped(const std::string& filePath) = 0;
            virtual void OnRecorderError(int code, const std::string& message) = 0;
            virtual ~Listener() = default;
        };

        virtual ~IRecorder() = default;

        static IRecorder* Create();

        virtual void SetListener(Listener* listener) = 0;
        virtual bool Start(const std::string& filePath, const RecorderConfig& config) = 0;
        virtual void Stop() = 0;
        virtual bool IsRecording() const = 0;
        virtual void WriteAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples) = 0;
        virtual void WriteVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame) = 0;
    };
}

#endif //IRECORDER_H
