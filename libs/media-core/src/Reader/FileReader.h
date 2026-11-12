//
// Created by Joe on 26-3-25.
//

#ifndef FILEREADER_H
#define FILEREADER_H
#include "Interface/IAudioDecoder.h"
#include "Interface/IDeMuxer.h"
#include "Interface/IFileReader.h"
#include "Interface/IVideoDecoder.h"


namespace av {
    class FileReader : public IFileReader,
                        public IDeMuxer::Listener,
                        public IAudioDecoder::Listener,
                        public IVideoDecoder::Listener {
    public:
        FileReader();
        ~FileReader() override;

        //设置回调器
        void SetListener(IFileReader::Listener *listener) override;
        //继承IFileReader
        bool Open(std::string &filePath) override;
        void Start() override;
        void Paused() override;
        void SeekTo(float position) override;
        void Stop() override;

        float GetDuration() override;
        int GetVideoWidth() override;
        int GetVideoHeight() override;

    private:
        //继承IDeMuxer::Listener
        void OnDeMuxStart() override;
        void OnDeMuxStop() override;
        void OnDeMuxEOF() override;
        void OnDeMuxError(int code, const char *msg) override;

        void  OnNotifyAudioStream(struct AVStream *stream) override;
        void OnNotifyVideoStream(struct AVStream *stream) override;

        void OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) override;
        void OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) override;

        //IAudioDecoder::Listener
        void OnNotifyAudioSamples(std::shared_ptr<IAudioSamples>) override;
        //IVideoDecoder::Listener
        void OnNotifyVideoFrame(std::shared_ptr<IVideoFrame>) override;

        //数据回调
        IFileReader::Listener* m_listener{nullptr};
        std::recursive_mutex m_listenerMutex;

        //解封装器
        std::shared_ptr<IDeMuxer> m_deMuxer;

        //解码器
        std::shared_ptr<IAudioDecoder> m_audioDecoder;
        std::shared_ptr<IVideoDecoder> m_videoDecoder;
    };
}



#endif //FILEREADER_H
