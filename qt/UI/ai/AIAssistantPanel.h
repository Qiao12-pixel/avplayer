#ifndef QT_UI_AI_AIASSISTANTPANEL_H
#define QT_UI_AI_AIASSISTANTPANEL_H

#include <QLabel>
#include <QPushButton>
#include <QWidget>
/*
 * @brief 只负责 AI 侧栏布局、控件、展示状态和发出用户操作信号。
 */
class AIAssistantPanel final : public QWidget {
    Q_OBJECT
public:
    explicit AIAssistantPanel(QWidget* parent = nullptr);
    void SetMediaContext(const QString& source, bool playing, int width = 0, int height = 0);
    void SetRecording(bool recording);

signals:
    void playPauseRequested();
    void seekToStartRequested();

private:
    QLabel* m_sourceLabel{nullptr};
    QLabel* m_stateLabel{nullptr};
    QLabel* m_recordLabel{nullptr};
    QLabel* m_resolutionLabel{nullptr};
    QPushButton* m_playPauseButton{nullptr};
    QPushButton* m_seekToStartButton{nullptr};
};

#endif // QT_UI_AI_AIASSISTANTPANEL_H
