//
// Created by Joe on 26-3-3.
//

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QJsonObject>
#include <QTimer>

#include <functional>

#include <iostream>
#include <atomic>

#include "Engine/AVSynchronizer.h"
#include "Network/NetworkSession.h"
#include "Reader/Interface/IFileReader.h"
#include "Writer/IRecorder.h"
#include "Writer/IRecordSession.h"
#include "AI/AIAssistantService.h"
#include "Agent/AgentBridge.h"
#include "Agent/ToolDispatcher.h"
#include "Streaming/WebRtcPlaybackSource.h"
#include "./ui/ClickableSlider.h"
#include "./ui/PlayerWidget.h"
#include "ui/ControllerWidget.h"
#include "ui/ai/AIAssistantPanel.h"
#include "ui/agent/AgentPanel.h"

namespace av {
    class FileReader;
}

class MainWindow : public QMainWindow,
                   public av::IFileReader::Listener,
                   public av::IRecordSession::Listener,
                   public av::ai::IPlayerControl {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void StartBenchmark(const QString& filePath, int seconds, bool sticker,
                        bool record, const QString& outputPath, const QString& capturePath);

    void OnFileReaderNotifyAudioSamples(std::shared_ptr<av::IAudioSamples> audioSamples) override;
    void OnFileReaderNotifyVideoFrame(std::shared_ptr<av::IVideoFrame> videoFrame) override;
    void OnFileReaderNotifyAudioFinished() override;
    void OnFileReaderNotifyVideoFinished() override;
    void OnRecordSessionStarted(const std::string& tempFilePath) override;
    void OnRecordSessionSaved(const std::string& outputFilePath) override;
    void OnRecordSessionAborted() override;
    void OnRecordSessionError(const std::string& message) override;
    void TogglePlayback() override;
    bool SeekToStart() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSliderMoved(int value);
    void onImportRequested(const QString& filePath);
    void onNetworkPlayRequested();
    void onNetworkStopRequested();
    void onPlayToggled(bool playing);
    void onRecordToggled(bool recording);
    void onScreenshotRequested();
    void onTick();
    void updateStreamingStatus();


private:

    struct UploadOutcome {
        bool ok{false};
        QString mediaId;
        QString error;
    };
    void uploadMediaAsync(const QString& sessionId, const QString& filePath,
                          std::function<void(const UploadOutcome&)> done);
    void connectStreamingPlayback(const QString& filePath, const QString& mediaId);
    void startDesktopUpload(const QString& filePath, bool connectWhenReady);
    void submitAiTask(const QString& task, const QJsonObject& arguments);
    void createFileReader();
    void releaseFileReader();
    void startPlayback(const QString& filePath);
    void stopRecording(bool promptForSavePath = false);
    void updateProgress();
    void appendDebugMessage(const QString& message);
    void refreshNetworkPanel();
    void syncAiContext();
    void setupAgentRuntime();
    void handleAgentMessage(const av::agent::AgentMessage& message);
    bool SaveScreenshotToPath(const QString& path);

    ClickableSlider* m_progressSlider = nullptr;
    QWidget* m_sidePanel = nullptr;
    AIAssistantPanel* m_aiAssistantPanel{nullptr};
    AgentPanel* m_agentPanel{nullptr};
    PlayerWidget* m_playerWidget = nullptr;
    QLabel* m_currentSourceValueLabel = nullptr;
    QLabel* m_networkStatusValueLabel = nullptr;
    QLabel* m_errorValueLabel = nullptr;
    QLabel* m_debugLabel = nullptr;
    QLineEdit* m_networkUrlEdit = nullptr;
    QPushButton* m_networkPlayButton = nullptr;
    QPushButton* m_networkStopButton = nullptr;
    ControllerWidget* m_controllerWidget{nullptr};
    QTimer m_renderTimer;
    QTimer m_streamStatusTimer;
    QString m_currentFilePath;
    bool m_isPlaying{false};
    bool m_hasOpenedFile{false};
    float m_durationSeconds{0.0f};
    QStringList m_debugMessages;
    av::NetworkSession m_networkSession;
    av::AVSynchronizer m_synchronizer;
    av::ai::AIAssistantService m_aiAssistantService;
    av::agent::AgentBridge m_agentBridge;
    av::agent::ToolDispatcher m_agentTools;
    QString m_desktopMediaId;
    QString m_desktopMediaFilePath;
    QString m_aiMediaId;
    QString m_aiMediaFilePath;
    bool m_desktopUploadInProgress{false};
    bool m_aiUploadInProgress{false};
    QString m_pendingGoal;
    QString m_pendingAiTask;
    QJsonObject m_pendingAiArguments;
    QString m_agentPendingRequestId;
    QString m_agentPendingSessionId;
    QString m_agentPendingTool;
    av::FileReader* m_fileReader{nullptr};
    std::unique_ptr<av::WebRtcPlaybackSource> m_streamingSource;
    bool m_streamingMode{false};
    bool m_pendingStreamingRecord{false};
    QString m_mediaServerAddress{"localhost:50062"};
    std::unique_ptr<av::IRecordSession> m_recordSession;
    std::atomic<uint64_t> m_playbackGeneration{0};
    QString m_aiProvider{"local"};
};

#endif //MAINWINDOW_H
