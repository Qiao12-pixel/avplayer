#ifndef QT_UI_AI_AIASSISTANTPANEL_H
#define QT_UI_AI_AIASSISTANTPANEL_H

#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
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
    void SetConfiguration(bool chatReady, bool speechReady, const QString& status);
    void SetTranscript(const QString& sourcePath);
    void SetSubtitleAvailable(bool available);
    void SetTaskStarted(const QString& name);
    void SetTaskProgress(const QString& name, const QString& message);
    void SetTaskResult(const QString& name, const QString& result);
    void SetTaskError(const QString& name, const QString& error);
    void SetEditPlanReady(bool ready);

signals:
    void playPauseRequested();
    void seekToStartRequested();
    void transcriptionRequested();
    void summaryRequested();
    void searchRequested(const QString& query);
    void editPlanRequested(const QString& instruction);
    void exportEditPlanRequested();
    void subtitleVisibilityChanged(bool visible);

private:
    QLabel* m_sourceLabel{nullptr};
    QLabel* m_stateLabel{nullptr};
    QLabel* m_recordLabel{nullptr};
    QLabel* m_resolutionLabel{nullptr};
    QPushButton* m_playPauseButton{nullptr};
    QPushButton* m_seekToStartButton{nullptr};
    QPushButton* m_transcribeButton{nullptr};
    QPushButton* m_summaryButton{nullptr};
    QPushButton* m_searchButton{nullptr};
    QPushButton* m_editButton{nullptr};
    QPushButton* m_exportButton{nullptr};
    QCheckBox* m_subtitleToggle{nullptr};
    QLineEdit* m_promptEdit{nullptr};
    QPlainTextEdit* m_resultEdit{nullptr};
    QLabel* m_connectionLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    bool m_hasMedia{false};
    bool m_hasTranscript{false};
    bool m_chatReady{false};
    bool m_speechReady{false};
    bool m_hasPlan{false};
    bool m_taskBusy{false};
    QString m_configStatus;
    void RefreshTools();
};

#endif // QT_UI_AI_AIASSISTANTPANEL_H
