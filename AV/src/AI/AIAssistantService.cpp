#include "AIAssistantService.h"

#include <utility>

namespace av::ai {
    AIAssistantService::AIAssistantService(IPlayerControl& playerControl)
        : m_playerControl(playerControl) {}

    void AIAssistantService::UpdateMediaContext(MediaContext context) {
        m_mediaContext = std::move(context);
    }

    const MediaContext& AIAssistantService::GetMediaContext() const {
        return m_mediaContext;
    }

    bool AIAssistantService::TogglePlayback() {
        if (!m_mediaContext.loaded) return false;
        m_playerControl.TogglePlayback();
        return true;
    }

    bool AIAssistantService::SeekToStart() {
        if (!m_mediaContext.loaded) return false;
        return m_playerControl.SeekToStart();
    }
}
