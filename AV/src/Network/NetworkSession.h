//
// Created by Codex on 26-3-27.
//

#ifndef NETWORKSESSION_H
#define NETWORKSESSION_H

#include <string>

namespace av {
    enum class NetworkSessionState {
        kIdle = 0,
        kConnecting,
        kPlaying,
        kStopped,
        kError,
    };

    class NetworkSession {
    public:
        void Start(const std::string& url);
        void Stop();
        void SetError(const std::string& errorText);

        [[nodiscard]] bool HasActiveSource() const;
        [[nodiscard]] bool IsNetworkSource() const;
        [[nodiscard]] const std::string& GetUrl() const;
        [[nodiscard]] const std::string& GetLastError() const;
        [[nodiscard]] NetworkSessionState GetState() const;
        [[nodiscard]] std::string GetStateText() const;

        static bool IsSupportedUrl(const std::string& url);
        static bool IsNetworkUrl(const std::string& url);
        static std::string DescribeSource(const std::string& url);

    private:
        std::string m_url;
        std::string m_lastError;
        NetworkSessionState m_state{NetworkSessionState::kIdle};
    };
}

#endif //NETWORKSESSION_H
