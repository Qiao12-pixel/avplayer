#ifndef AV_AI_IPLAYERCONTROL_H
#define AV_AI_IPLAYERCONTROL_H

namespace av::ai {
    class IPlayerControl {
    public:
        virtual ~IPlayerControl() = default;
        virtual void TogglePlayback() = 0;
        virtual bool SeekToStart() = 0;
    };
}

#endif // AV_AI_IPLAYERCONTROL_H
