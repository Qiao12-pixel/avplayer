//
// Created by Joe on 26-3-23.
//

#ifndef DEMUXER_H
#define DEMUXER_H
#include "Interface/IDeMuxer.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "../Core/SyncNotifier.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace av {

    class DeMuxer : public IDeMuxer{
    public:
        DeMuxer();
        ~DeMuxer() override;

        void SetListener(Listener *listener) override;//设置监听器进行回调上层

        bool Open(const std::string &url) override;
        void Start() override;
        void Pause() override;
        void SeekTo(float position) override;
        void Stop() override;

        float GetDuration() override;

    private:

        void ThreadLoop();
        void ProcessSeek();
        bool ReadAndSendPacket();

        void ReleaseAudioPipelineResource();
        void ReleaseVideoPipelineResource();

        struct StreamInfo {
            int streamIndex{-1};
            std::atomic<int> pipelineResourceCount{3};
            std::shared_ptr<std::function<void()>> pipelineReleaseCallback;
        };
        IDeMuxer::Listener* m_listener{nullptr};
        std::recursive_mutex m_listenerMutex;
        std::thread m_thread;
        std::atomic<bool> m_paused{true};
        std::atomic<bool> m_abort{false};
        std::atomic<bool> m_seek{false};
        SyncNotifier m_notifier;

        AVFormatContext* m_fmtCtx{nullptr};
        std::mutex m_fmtCtxMutex;
        StreamInfo m_audioStream;
        StreamInfo m_videoStream;

        float m_seekProgress{-1.0f};
    };
}



#endif //DEMUXER_H
