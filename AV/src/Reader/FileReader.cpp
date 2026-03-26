//
// Created by Joe on 26-3-25.
//

#include "FileReader.h"

#include <iostream>


namespace av {
    IFileReader *IFileReader::Create() {
        return new FileReader();
    }
    FileReader::FileReader() {
        //解封装器
        m_deMuxer = std::shared_ptr<IDeMuxer>(IDeMuxer::Create());

        //音视频解码器
        m_audioDecoder = std::shared_ptr<IAudioDecoder>(IAudioDecoder::Create(2, 44100));
        m_videoDecoder = std::shared_ptr<IVideoDecoder>(IVideoDecoder::Create());

        //串联各个模块：DeMuxer->AudioDecoder
        //                   ->VideoDecoder
        m_deMuxer->SetListener(this);
        m_audioDecoder->SetListener(this);
        m_videoDecoder->SetListener(this);
    }
    FileReader::~FileReader() {
        m_deMuxer->Pause();
        m_deMuxer = nullptr;
        m_audioDecoder = nullptr;
        m_videoDecoder = nullptr;
    }
    //设置监听器【修改判断：这里可以传入渲染层面，回调需要的frame】
    void FileReader::SetListener(IFileReader::Listener *listener) {
        std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
        m_listener = listener;
    }
    bool FileReader::Open(std::string &filePath) {
        if (!m_deMuxer) {
            std::cerr << "failed to IDeMuxer init." << std::endl;
            return false;
        }
        std::cout << "FileReader::Open path: " << filePath << std::endl;
        return m_deMuxer->Open(filePath);
    }
    void FileReader::Start() {
        if (m_deMuxer) {
            m_deMuxer->Start();
        } if (m_audioDecoder) {
            m_audioDecoder->Start();
        } if (m_videoDecoder) {
            m_videoDecoder->Start();
        }
    }
    void FileReader::Paused() {
        if (m_deMuxer) {
            m_deMuxer->Pause();
        } if (m_audioDecoder) {
            m_audioDecoder->Pause();
        } if (m_videoDecoder) {
            m_videoDecoder->Pause();
        }
    }
    void FileReader::Stop() {
        if (m_deMuxer) {
            m_deMuxer->Stop();
        } if (m_audioDecoder) {
            m_audioDecoder->Stop();
        } if (m_videoDecoder) {
            m_videoDecoder->Stop();
        }
    }
    void FileReader::SeekTo(float position) {
        if (m_deMuxer) {
            m_deMuxer->SeekTo(position);
        }
    }
    float FileReader::GetDuration() {
        return m_deMuxer ? m_deMuxer->GetDuration() : 0;
    }
    int FileReader::GetVideoWidth() {
        return m_videoDecoder ? m_videoDecoder->GetVideoWidth() : 0;
    }
    int FileReader::GetVideoHeight() {
        return m_videoDecoder ? m_videoDecoder->GetVideoHeight() : 0;
    }

    //继承IDeMuxer::Listener
    void FileReader::OnDeMuxStart() {
        std::cout << "FileReader::OnDeMuxStart" << std::endl;
    }

    void FileReader::OnDeMuxStop() {
        std::cout << "FileReader::OnDeMuxStop" << std::endl;
    }

    void FileReader::OnDeMuxEOF() {
        std::cout << "FileReader::OnDeMuxEOF" << std::endl;
    }

    void FileReader::OnDeMuxError(int code, const char* msg) {
        std::cout << "FileReader::OnDeMuxError" << std::endl;
    }
    void FileReader::OnNotifyAudioStream(struct AVStream *stream) {
        if (m_audioDecoder) {
            m_audioDecoder->SetStream(stream);
        }
    }
    //??通过什么方式
    void FileReader::OnNotifyVideoStream(struct AVStream *stream) {
        if (m_videoDecoder) {
            m_videoDecoder->SetStream(stream);
        }
    }
    void FileReader::OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) {
        if (m_audioDecoder) {
            m_audioDecoder->Decode(packet);
        }
    }
    void FileReader::OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) {
        if (m_videoDecoder) {
            m_videoDecoder->Decode(packet);
        }
    }
    //继承IAudioDecoder::Listener
    void FileReader::OnNotifyAudioSamples(std::shared_ptr<IAudioSamples> audioSamples) {
        std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
        if (m_listener) {
            m_listener->OnFileReaderNotifyAudioSamples(audioSamples);
        }
    }


    //IVideoDecoder::Listener
    void FileReader::OnNotifyVideoFrame(std::shared_ptr<IVideoFrame> videoFrame) {
        std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
        if (m_listener) {
            m_listener->OnFileReaderNotifyVideoFrame(videoFrame);
        }
    }

















}
