//
// Created by Joe on 26-3-25.
//

#ifndef IFILEREADER_H
#define IFILEREADER_H
#include <memory>

#include "../../Define/IAudioSamples.h"
#include "../../Define/IVideoFrame.h"

namespace av {
    struct IFileReader {
        struct Listener {
            virtual void OnFileReaderNotifyAudioSamples(std::shared_ptr<IAudioSamples>) = 0;
            virtual void OnFileReaderNotifyVideoFrame(std::shared_ptr<IVideoFrame>) = 0;
            virtual void OnFileReaderNotifyAudioFinished() = 0;
            virtual void OnFileReaderNotifyVideoFinished() = 0;
            virtual ~Listener() = default;
        };
        virtual void SetListener(Listener* listener) = 0;

        virtual bool Open(std::string& filePath) = 0;
        virtual void Start() = 0;
        virtual void Paused() = 0;
        virtual void Stop() = 0;
        virtual void SeekTo(float position) = 0;

        virtual float GetDuration() = 0;
        virtual int GetVideoWidth() = 0;
        virtual int GetVideoHeight() = 0;

        virtual ~IFileReader() = default;

        static IFileReader* Create();
    };
}
#endif //IFILEREADER_H
