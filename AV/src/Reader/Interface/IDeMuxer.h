//
// Created by Joe on 26-3-23.
//

#ifndef IDEMUXER_H
#define IDEMUXER_H
#include <string>
#include "../../Define/IAVPacket.h"
struct AVStream;
namespace av {
    struct IDeMuxer {
        struct Listener {
            //由继承IDMuxer::Listener实现
            virtual void OnDeMuxStart() = 0;
            virtual void OnDeMuxStop() = 0;
            virtual void OnDeMuxEOF() = 0;
            virtual void OnDeMuxError(int code, const char* msg) = 0;

            virtual void OnNotifyAudioStream(struct AVStream* stream) = 0;
            virtual void OnNotifyVideoStream(struct AVStream* stream) = 0;

            virtual void OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) = 0;
            virtual void OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) = 0;

            virtual ~Listener() = default;
        };
        virtual void SetListener(Listener* listener) = 0;//监听器绑定：设置回调实现，让解封装器能触发上层回调

        virtual bool Open(const std::string& url) = 0;
        virtual void Start() = 0;
        virtual void Pause() = 0;
        virtual void SeekTo(float position) = 0;
        virtual void Stop() = 0;

        virtual float GetDuration() = 0;

        virtual ~IDeMuxer() = default;

        static IDeMuxer* Create();
    };
}



#endif //IDEMUXER_H
