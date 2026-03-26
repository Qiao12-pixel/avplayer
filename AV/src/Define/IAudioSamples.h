//
// Created by Joe on 26-3-24.
//

#ifndef IAUDIOSAMPLES_H
#define IAUDIOSAMPLES_H
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "BaseDef.h"

namespace av {

    struct IAudioSamples {
        int flags{0};             ///<  标志位
        unsigned int channels;    ///< 通道数
        unsigned int sampleRate;  ///<  采样率
        // ESampleFormat sampleFormat; ///< 统一使用16位整数

        int64_t pts;          ///< 显示时间戳
        int64_t duration;     ///< 持续时间
        int32_t timebaseNum;  ///< 时间基数分子
        int32_t timebaseDen;  ///< 时间基数分母

        std::vector<int16_t> pcmData;  ///<  存储PCM数据的向量
        size_t offset{0};              ///<  当前PCM数据的偏移量

        std::weak_ptr<std::function<void()>> releaseCallback;  ///<  释放的回调函数

        float GetTimeStamp() const { return pts * 1.0f * timebaseNum / timebaseDen; }  ///<  获取时间戳
        virtual ~IAudioSamples() {
            if (auto lockedPtr = releaseCallback.lock()) {
                (*lockedPtr)();
            }
        }
    };

}  // namespace av
#endif //IAUDIOSAMPLES_H
