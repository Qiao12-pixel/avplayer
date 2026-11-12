//
// Created by Joe on 26-3-25.
//

#ifndef VIDEODECODER_H
#define VIDEODECODER_H
#include <list>
#include <thread>
#include "../Core/SyncNotifier.h"
#include "Interface/IVideoDecoder.h"


//视频解码--接受解复用下来的stream,packet

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
namespace av {
    class VideoDecoder : public IVideoDecoder{
    public:
        VideoDecoder();
        ~VideoDecoder() override;
        //重载父类函数
        void SetListener(Listener *listener) override;
        void SetStream(struct AVStream *stream) override;
        void Decode(std::shared_ptr<IAVPacket> packet) override;
        void Start() override;
        void Pause() override;
        void Stop() override;

        int GetVideoWidth() override;
        int GetVideoHeight() override;
    private:
        void ThreadLoop();
        void checkFlushPacket();
        void DecodeAVPacket();
        void ReleaseVideoPipelineResource();
        void CLeanupContext();

    private:
        //数据回调
        IVideoDecoder::Listener* m_listener{nullptr};
        std::recursive_mutex m_listenerMutex;

        //解码 && 缩放
        std::mutex m_codecContexMutex;
        AVCodecContext* m_codecContex{nullptr};
        SwsContext* m_swsContext{nullptr};
        AVRational m_timeBase{AVRational{1, 1}};

        //AVPacket队列
        std::list<std::shared_ptr<IAVPacket>> m_packetQueue;
        std::mutex m_packetQueueMutex;

        SyncNotifier m_notifier;

        std::thread m_thread;
        std::atomic<bool> m_paused{true};
        std::atomic<bool> m_abort{false};
        std::atomic<int> m_pipelineResourceCount{3};
        std::shared_ptr<std::function<void()>> m_pipelineReleaseCallback;

    };
}



#endif //VIDEODECODER_H
