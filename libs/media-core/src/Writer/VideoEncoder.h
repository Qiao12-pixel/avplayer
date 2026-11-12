//
// Created by Codex on 26-3-28.
//

#ifndef VIDEOENCODER_H
#define VIDEOENCODER_H

#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include "IRecorder.h"
#include "Interface/IVideoEncoder.h"

namespace av {
    class VideoEncoder final : public IVideoEncoder {
    public:
        VideoEncoder();
        ~VideoEncoder() override;

        void SetListener(Listener* listener) override;
        bool Open(const RecorderConfig& config, bool globalHeader) override;
        void Encode(const std::shared_ptr<IVideoFrame>& videoFrame, int64_t recordingStartUs) override;
        void Flush() override;
        void Close() override;
        AVCodecContext* GetCodecContext() const override;

    private:
        bool EmitPackets(AVFrame* frame);
        int64_t GetTimestampUs(int64_t pts, int32_t timebaseNum, int32_t timebaseDen) const;
        void CleanupLocked();
        void LogError(const char* prefix, int errorCode) const;

        mutable std::mutex m_mutex;
        Listener* m_listener{nullptr};
        AVCodecContext* m_codecContext{nullptr};
        SwsContext* m_swsContext{nullptr};
        int64_t m_lastVideoPts{AV_NOPTS_VALUE};
    };
}

#endif //VIDEOENCODER_H
