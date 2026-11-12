//
// Created by Joe on 26-3-25.
//

#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <list>
#include <thread>
#include "../Core/SyncNotifier.h"
#include "Interface/IAudioDecoder.h"


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
namespace av {
    class AudioDecoder : public IAudioDecoder {
    public:
        AudioDecoder(unsigned int channels, unsigned int sampleRate);
        ~AudioDecoder() override;
        //继承IAudioDecoder
        void SetListener(Listener* listener) override;
        void SetStream(struct AVStream* stream) override;
        void Decode(std::shared_ptr<IAVPacket> packet) override;
        void Start() override;
        void Pause() override;
        void Stop() override;
    private:
        void ThreadLoop();
        void CheckFlushPacket();
        void DecodeAVPacket();
        void ReleaseAudioPipelineResource();
        void CleanupContext();

        unsigned int m_targetChannels;
        unsigned int m_targetSampleRate;

        //数据回调
        IAudioDecoder::Listener* m_listener{nullptr};
        std::recursive_mutex m_listenerMutex;


        //解码&& 重采样
        std::mutex m_codecContextMutex;
        AVCodecContext* m_codecContext{nullptr};
        SwrContext* m_swrContext{nullptr};
        AVRational m_timeBase{AVRational{1,1}};

        //AVPacket队列
        std::list<std::shared_ptr<IAVPacket>> m_packetQueue;
        std::mutex m_packetQueueMutex;
        //线程同步通知
        SyncNotifier m_notifier;

        std::thread m_thread;
        std::atomic<bool> m_paused{true};
        std::atomic<bool> m_abort{false};
        std::atomic<int> m_pipelineResourceCount{3};
        std::shared_ptr<std::function<void()>> m_pipelineReleaseCallback;
    };
}



#endif //AUDIODECODER_H
