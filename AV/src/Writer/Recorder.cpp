//
// Created by Codex on 26-3-28.
//

#include "Recorder.h"

#include <iostream>

#include "AudioEncoder.h"
#include "Mp4Muxer.h"
#include "VideoEncoder.h"

namespace av {
    IRecorder* IRecorder::Create() {
        return new Recorder();
    }

    Recorder::Recorder() {
        m_audioEncoder.reset(IAudioEncoder::Create());
        m_videoEncoder.reset(IVideoEncoder::Create());
        if (m_audioEncoder) {
            m_audioEncoder->SetListener(this);
        }
        if (m_videoEncoder) {
            m_videoEncoder->SetListener(this);
        }
    }

    Recorder::~Recorder() {
        Stop();
    }

    void Recorder::SetListener(IRecorder::Listener* listener) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listener = listener;
    }

    bool Recorder::Start(const std::string& filePath, const RecorderConfig& config) {
        std::lock_guard<std::mutex> lock(m_mutex);
        Cleanup();
        if (filePath.empty() || config.width <= 0 || config.height <= 0) {
            return false;
        }
        if (!InitializeOutput(filePath, config)) {
            Cleanup();
            return false;
        }
        m_filePath = filePath;
        m_config = config;
        m_recording = true;
        std::cout << "[Recorder] recording started: " << m_filePath << std::endl;
        if (m_listener) {
            m_listener->OnRecorderStarted(m_filePath);
        }
        return true;
    }

    void Recorder::Stop() {
        IAudioEncoder* audioEncoder = nullptr;
        IVideoEncoder* videoEncoder = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_muxer && !m_recording) {
                return;
            }
            audioEncoder = m_audioEncoder.get();
            videoEncoder = m_videoEncoder.get();
        }

        if (audioEncoder) {
            audioEncoder->Flush();
        }
        if (videoEncoder) {
            videoEncoder->Flush();
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_muxer) {
            m_muxer->Close();
        }
        std::cout << "[Recorder] recording stopped" << std::endl;
        if (m_listener) {
            m_listener->OnRecorderStopped(m_filePath);
        }
        Cleanup();
    }

    bool Recorder::IsRecording() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_recording;
    }

    void Recorder::WriteAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples) {
        IAudioEncoder* audioEncoder = nullptr;
        int64_t recordingStartUs = AV_NOPTS_VALUE;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_recording || !audioSamples || (audioSamples->flags & AVFrameFlag::KFlush) != 0) {
                return;
            }
            audioEncoder = m_audioEncoder.get();
            recordingStartUs = m_recordingStartUs;
        }
        if (!audioEncoder || recordingStartUs == AV_NOPTS_VALUE) {
            return;
        }
        audioEncoder->Encode(audioSamples, recordingStartUs);
    }

    void Recorder::WriteVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame) {
        IVideoEncoder* videoEncoder = nullptr;
        int64_t recordingStartUs = AV_NOPTS_VALUE;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_recording || !videoFrame || !videoFrame->data || (videoFrame->flags & AVFrameFlag::KFlush) != 0) {
                return;
            }
            const int64_t videoTimestampUs = GetTimestampUs(videoFrame->pts, videoFrame->timebaseNum, videoFrame->timebaseDen);
            if (videoTimestampUs != AV_NOPTS_VALUE && m_recordingStartUs == AV_NOPTS_VALUE) {
                m_recordingStartUs = videoTimestampUs;
                std::cout << "[Recorder] recording epoch anchored to first video frame at "
                          << m_recordingStartUs << "us" << std::endl;
            }
            videoEncoder = m_videoEncoder.get();
            recordingStartUs = m_recordingStartUs;
        }
        if (!videoEncoder || recordingStartUs == AV_NOPTS_VALUE) {
            return;
        }
        videoEncoder->Encode(videoFrame, recordingStartUs);
    }

    void Recorder::OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_muxer && m_recording) {
            m_muxer->NotifyAudioPacket(packet);
        }
    }

    void Recorder::OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_muxer && m_recording) {
            m_muxer->NotifyVideoPacket(packet);
        }
    }

    bool Recorder::InitializeOutput(const std::string& filePath, const RecorderConfig& config) {
        m_muxer = std::make_unique<Mp4Muxer>();
        m_muxer->SetListener(this);
        m_useGlobalHeader = true;
        if (!m_videoEncoder || !m_videoEncoder->Open(config, m_useGlobalHeader)) {
            return false;
        }
        if (!m_audioEncoder || !m_audioEncoder->Open(config, m_useGlobalHeader)) {
            return false;
        }
        if (!m_muxer->Open(filePath, m_videoEncoder->GetCodecContext(), m_audioEncoder->GetCodecContext())) {
            std::cerr << "[Recorder] muxer open failed" << std::endl;
            return false;
        }
        return true;
    }

    void Recorder::OnMuxerOpen(const std::string& filePath) {
        std::cout << "[Recorder] muxer opened: " << filePath << std::endl;
    }

    void Recorder::OnMuxerClose(const std::string& filePath) {
        std::cout << "[Recorder] muxer closed: " << filePath << std::endl;
    }

    void Recorder::OnMuxerError(int code, const std::string& message) {
        std::cerr << "[Recorder] muxer error code=" << code << " msg=" << message << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_listener) {
            m_listener->OnRecorderError(code, message);
        }
    }

    int64_t Recorder::GetTimestampUs(int64_t pts, int32_t timebaseNum, int32_t timebaseDen) const {
        if (pts == AV_NOPTS_VALUE || timebaseNum <= 0 || timebaseDen <= 0) {
            return AV_NOPTS_VALUE;
        }
        return av_rescale_q(pts, AVRational{timebaseNum, timebaseDen}, AV_TIME_BASE_Q);
    }

    void Recorder::Cleanup() {
        if (m_audioEncoder) {
            m_audioEncoder->Close();
        }
        if (m_videoEncoder) {
            m_videoEncoder->Close();
        }
        m_muxer.reset();
        ResetState();
    }

    void Recorder::ResetState() {
        m_recording = false;
        m_filePath.clear();
        m_config = {};
        m_recordingStartUs = AV_NOPTS_VALUE;
        m_useGlobalHeader = true;
    }
}
