#ifndef AV_AI_AIASSISTANTSERVICE_H
#define AV_AI_AIASSISTANTSERVICE_H

#include <string>

#include "IPlayerControl.h"
/*
 * @brief 负责 AI Assistant 的媒体上下文、播放器控制接口和功能服务。
 */
namespace av::ai {
    struct MediaContext {
        std::string source;
        bool loaded{false};
        bool playing{false};
        bool recording{false};
        int width{0};
        int height{0};
    };

    class AIAssistantService final {
    public:
        explicit AIAssistantService(IPlayerControl& playerControl);

        void UpdateMediaContext(MediaContext context);
        const MediaContext& GetMediaContext() const;

        bool TogglePlayback();
        bool SeekToStart();

    private:
        IPlayerControl& m_playerControl;
        MediaContext m_mediaContext;
    };
}

#endif // AV_AI_AIASSISTANTSERVICE_H
