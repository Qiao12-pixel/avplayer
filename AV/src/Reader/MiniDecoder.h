//
// Created by Joe on 26-3-24.
//

#ifndef MINIDECODER_H
#define MINIDECODER_H
#include "Interface/IDeMuxer.h"
#include <iostream>

namespace av {
    class MiniDecoder : public IDeMuxer::Listener{
    public:
        MiniDecoder() = default;
        ~MiniDecoder()  override;
        //继承IDeMuxer::Listener
        void OnDeMuxStart() override { std::cout << "Demux Started\n"; }
        void OnDeMuxStop() override { std::cout << "Demux Stopped\n"; }
        void OnDeMuxEOF() override { std::cout << "Demux EOF\n"; }
        void OnDeMuxError(int code, const char* msg) override { std::cout << "Error: " << msg << "\n"; }

        void OnNotifyAudioStream(struct AVStream *stream) override;
        void OnNotifyVideoStream(struct AVStream *stream) override;
        void OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) override;
        void OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) override;


    private:
        AVCodecContext* m_audioCodecCtx{nullptr};
        AVCodecContext* m_videoCodecCtx{nullptr};

        //辅助函数：释放包含的资源
        void ReleasePacket(std::shared_ptr<IAVPacket>& packet);

    };
}



#endif //MINIDECODER_H
