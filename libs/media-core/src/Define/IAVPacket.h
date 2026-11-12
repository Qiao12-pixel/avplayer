//
// Created by Joe on 26-3-23.
//

#ifndef IAVPACKET_H
#define IAVPACKET_H
#include <functional>
#include <memory>//智能指针
#include "BaseDef.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>//处理小数、分数
}

namespace av {
    struct IAVPacket {
        int flags{0};//标志位
        struct AVPacket* avPacket{nullptr};
        AVRational timeBase{AVRational{0, 0}};
        std::weak_ptr<std::function<void()>> releaseCallback;

        explicit IAVPacket(struct AVPacket* avPacket) : avPacket(avPacket){}
        virtual ~IAVPacket() {
            if (avPacket) {
                av_packet_free(&avPacket);
            }
            if (auto lockedPtr = releaseCallback.lock()) {
                (*lockedPtr)();
            }
        }
        float GetTimeStamp() const {
            return avPacket ? avPacket->pts * 1.0f *timeBase.num / timeBase.den : -1.0f;
        }
    };
}
#endif //IAVPACKET_H
