//
// Created by Joe on 26-3-24.
//

#ifndef IVIDEOFRAME_H
#define IVIDEOFRAME_H
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "BaseDef.h"
#include "../Engine/IGLContext.h"

namespace av {
    struct IVideoFrame {
        int flags{0};                   ///<  标志位
        unsigned int width{0};          ///< 视频帧的宽度
        unsigned int height{0};         ///< 视频帧的高度
        int64_t pts{0};                 ///< 显示时间戳
        int64_t duration{0};            ///< 持续时间
        int32_t timebaseNum{1};         ///< 时间基数的分子
        int32_t timebaseDen{1};         ///< 时间基数的分母
        std::shared_ptr<uint8_t> data;  ///< RGBA 数据，为了简化代码，统一使用 RGBA 数据
        unsigned int textureId{0};      ///< OpenGL 纹理 ID

        std::weak_ptr<std::function<void()>> releaseCallback;  ///< 释放的回调函数

        float GetTimeStamp() const { return pts * 1.0f * timebaseNum / timebaseDen; }  ///< 获取时间戳
        virtual ~IVideoFrame() {
            if (textureId > 0) glDeleteTextures(1, &textureId);
            if (auto lockedPtr = releaseCallback.lock()) {
                (*lockedPtr)();
            }
        }
    };

}  // namespace av
#endif //IVIDEOFRAME_H
