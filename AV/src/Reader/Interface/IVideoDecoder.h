//
// Created by Joe on 26-3-25.
//

#ifndef IVIDEODECODER_H
#define IVIDEODECODER_H
#include <memory>

#include "../../Define/IAVPacket.h"
#include "../../Define/IVideoFrame.h"
//解码器解码出来的肯定是初始frame
struct AVStream;
namespace av {
    struct IVideoDecoder {
        struct Listener {
            virtual void OnNotifyVideoFrame(std::shared_ptr<IVideoFrame>) = 0;
            virtual ~Listener() = default;
        };

        virtual void SetListener(Listener* listener) = 0;
        virtual void SetStream(struct AVStream* stream) = 0;
        virtual void Decode(std::shared_ptr<IAVPacket> packet) = 0;
        virtual void Start() = 0;
        virtual void Pause() = 0;
        virtual void Stop() = 0;

        virtual int GetVideoWidth() = 0;
        virtual int GetVideoHeight() = 0;

        virtual ~IVideoDecoder() = default;

        static IVideoDecoder* Create();
    };
}
#endif //IVIDEODECODER_H
