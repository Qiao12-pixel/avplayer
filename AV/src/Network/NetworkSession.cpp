//
// Created by Codex on 26-3-27.
//

#include "NetworkSession.h"

#include <algorithm>

namespace av {
    namespace {
        std::string ToLower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool StartsWith(const std::string& value, const std::string& prefix) {
            return value.rfind(prefix, 0) == 0;
        }
    }

    void NetworkSession::Start(const std::string& url) {
        m_url = url;
        m_lastError.clear();
        m_state = IsNetworkUrl(url) ? NetworkSessionState::kConnecting : NetworkSessionState::kPlaying;
    }

    void NetworkSession::Stop() {
        m_state = NetworkSessionState::kStopped;
    }

    void NetworkSession::SetError(const std::string& errorText) {
        m_lastError = errorText;
        m_state = errorText.empty() ? m_state : NetworkSessionState::kError;
    }

    bool NetworkSession::HasActiveSource() const {
        return !m_url.empty();
    }

    bool NetworkSession::IsNetworkSource() const {
        return IsNetworkUrl(m_url);
    }

    const std::string& NetworkSession::GetUrl() const {
        return m_url;
    }

    const std::string& NetworkSession::GetLastError() const {
        return m_lastError;
    }

    NetworkSessionState NetworkSession::GetState() const {
        return m_state;
    }

    std::string NetworkSession::GetStateText() const {
        switch (m_state) {
            case NetworkSessionState::kIdle:
                return "空闲";
            case NetworkSessionState::kConnecting:
                return "连接中";
            case NetworkSessionState::kPlaying:
                return "播放中";
            case NetworkSessionState::kStopped:
                return "已停止";
            case NetworkSessionState::kError:
                return "连接失败";
        }
        return "未知状态";
    }

    bool NetworkSession::IsSupportedUrl(const std::string& url) {
        return IsNetworkUrl(url) || url.find("://") == std::string::npos;
    }

    bool NetworkSession::IsNetworkUrl(const std::string& url) {
        const std::string lowered = ToLower(url);
        return StartsWith(lowered, "http://") ||
               StartsWith(lowered, "https://") ||
               StartsWith(lowered, "rtmp://") ||
               StartsWith(lowered, "rtsp://");
    }

    std::string NetworkSession::DescribeSource(const std::string& url) {
        const std::string lowered = ToLower(url);
        if (StartsWith(lowered, "http://")) {
            return lowered.find(".m3u8") != std::string::npos ? "HLS/m3u8 流" : "HTTP 视频流";
        }
        if (StartsWith(lowered, "https://")) {
            return lowered.find(".m3u8") != std::string::npos ? "HLS/m3u8 流" : "HTTPS 视频流";
        }
        if (StartsWith(lowered, "rtmp://")) {
            return "RTMP 直播流";
        }
        if (StartsWith(lowered, "rtsp://")) {
            return "RTSP 视频流";
        }
        if (url.find("://") == std::string::npos) {
            return "本地文件";
        }
        return "未知来源";
    }
}
