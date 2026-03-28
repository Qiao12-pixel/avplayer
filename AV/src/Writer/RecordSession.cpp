//
// Created by Codex on 26-3-28.
//

#include "RecordSession.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace av {
    namespace fs = std::filesystem;

    IRecordSession* IRecordSession::Create() {
        return new RecordSession();
    }

    RecordSession::RecordSession() {
        m_recorder.reset(IRecorder::Create());
        if (m_recorder) {
            m_recorder->SetListener(this);
        }
    }

    RecordSession::~RecordSession() {
        Abort();
    }

    void RecordSession::SetListener(IRecordSession::Listener* listener) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listener = listener;
    }

    bool RecordSession::Start(const RecorderConfig& config) {
        IRecorder* recorder = nullptr;
        std::string tempFilePath;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_recorder) {
                NotifyErrorLocked("录制器未初始化。");
                return false;
            }
            if (m_recorder->IsRecording()) {
                NotifyErrorLocked("已有录制会话正在进行中。");
                return false;
            }

            tempFilePath = CreateTempFilePath();
            if (tempFilePath.empty()) {
                NotifyErrorLocked("无法创建临时录制路径。");
                return false;
            }

            m_tempFilePath = tempFilePath;
            recorder = m_recorder.get();
        }

        if (!recorder->Start(tempFilePath, config)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tempFilePath.clear();
            NotifyErrorLocked("录制启动失败。");
            return false;
        }
        return true;
    }

    bool RecordSession::StopAndSave(const std::string& outputFilePath) {
        IRecordSession::Listener* listener = nullptr;
        std::string tempFilePath;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            listener = m_listener;
            if (!m_recorder || !m_recorder->IsRecording()) {
                NotifyErrorLocked("当前没有录制中的会话。");
                return false;
            }
            tempFilePath = m_tempFilePath;
        }

        m_recorder->Stop();

        std::error_code ec;
        if (outputFilePath.empty()) {
            fs::remove(tempFilePath, ec);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tempFilePath.clear();
            NotifyErrorLocked("保存路径为空，录制文件已丢弃。");
            return false;
        }

        fs::path outputPath(outputFilePath);
        fs::create_directories(outputPath.parent_path(), ec);
        fs::remove(outputPath, ec);
        fs::rename(tempFilePath, outputPath, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(tempFilePath, outputPath, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                fs::remove(tempFilePath, ec);
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tempFilePath.clear();
        }

        if (ec) {
            if (listener) {
                listener->OnRecordSessionError("录制文件保存失败。");
            }
            return false;
        }

        if (listener) {
            listener->OnRecordSessionSaved(outputPath.string());
        }
        return true;
    }

    void RecordSession::Abort() {
        IRecordSession::Listener* listener = nullptr;
        std::string tempFilePath;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            listener = m_listener;
            tempFilePath = m_tempFilePath;
        }

        if (m_recorder && m_recorder->IsRecording()) {
            m_recorder->Stop();
        }

        std::error_code ec;
        if (!tempFilePath.empty()) {
            fs::remove(tempFilePath, ec);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tempFilePath.clear();
        }

        if (listener) {
            listener->OnRecordSessionAborted();
        }
    }

    bool RecordSession::IsRecording() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_recorder && m_recorder->IsRecording();
    }

    std::string RecordSession::GetTempFilePath() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tempFilePath;
    }

    std::string RecordSession::BuildDefaultOutputPath(const std::string& directory) const {
        const auto now = std::chrono::system_clock::now();
        const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &time);
#else
        localtime_r(&time, &localTm);
#endif
        std::ostringstream name;
        name << directory << "/record_"
             << (localTm.tm_year + 1900)
             << (localTm.tm_mon + 1 < 10 ? "0" : "") << (localTm.tm_mon + 1)
             << (localTm.tm_mday < 10 ? "0" : "") << localTm.tm_mday
             << "_"
             << (localTm.tm_hour < 10 ? "0" : "") << localTm.tm_hour
             << (localTm.tm_min < 10 ? "0" : "") << localTm.tm_min
             << (localTm.tm_sec < 10 ? "0" : "") << localTm.tm_sec
             << "_" << millis << ".mp4";
        return name.str();
    }

    void RecordSession::WriteAudioSamples(const std::shared_ptr<IAudioSamples>& audioSamples) {
        IRecorder* recorder = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            recorder = m_recorder.get();
        }
        if (recorder) {
            recorder->WriteAudioSamples(audioSamples);
        }
    }

    void RecordSession::WriteVideoFrame(const std::shared_ptr<IVideoFrame>& videoFrame) {
        IRecorder* recorder = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            recorder = m_recorder.get();
        }
        if (recorder) {
            recorder->WriteVideoFrame(videoFrame);
        }
    }

    void RecordSession::OnRecorderStarted(const std::string& filePath) {
        IRecordSession::Listener* listener = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            listener = m_listener;
        }
        if (listener) {
            listener->OnRecordSessionStarted(filePath);
        }
    }

    void RecordSession::OnRecorderStopped(const std::string& filePath) {
        std::cout << "[RecordSession] recorder stopped: " << filePath << std::endl;
    }

    void RecordSession::OnRecorderError(int code, const std::string& message) {
        IRecordSession::Listener* listener = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            listener = m_listener;
        }
        if (listener) {
            listener->OnRecordSessionError("[Recorder] code=" + std::to_string(code) + " " + message);
        }
    }

    std::string RecordSession::CreateTempFilePath() const {
        std::error_code ec;
        fs::path dir = fs::path(CACHE_DIR) / "recordings";
        fs::create_directories(dir, ec);
        if (ec) {
            return {};
        }
        const auto now = std::chrono::system_clock::now();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return (dir / ("recording_" + std::to_string(millis) + ".mp4")).string();
    }

    void RecordSession::NotifyErrorLocked(const std::string& message) const {
        if (m_listener) {
            m_listener->OnRecordSessionError(message);
        }
    }
}
