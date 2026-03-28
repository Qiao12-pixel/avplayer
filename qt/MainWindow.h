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
#include <QTimer>

#include <iostream>

#include "../AV/src/Engine/AVSynchronizer.h"
#include "../AV/src/Network/NetworkSession.h"
#include "../AV/src/Reader/Interface/IFileReader.h"
#include "../AV/src/Writer/IRecorder.h"
#include "../AV/src/Writer/IRecordSession.h"
#include "./UI/ClickableSlider.h"
#include "./UI/PlayerWidget.h"
#include "UI/ControllerWidget.h"

namespace av {
    class FileReader;
}

class MainWindow : public QMainWindow,
                   public av::IFileReader::Listener,
                   public av::IRecordSession::Listener {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void OnFileReaderNotifyAudioSamples(std::shared_ptr<av::IAudioSamples> audioSamples) override;
    void OnFileReaderNotifyVideoFrame(std::shared_ptr<av::IVideoFrame> videoFrame) override;
    void OnFileReaderNotifyAudioFinished() override;
    void OnFileReaderNotifyVideoFinished() override;
    void OnRecordSessionStarted(const std::string& tempFilePath) override;
    void OnRecordSessionSaved(const std::string& outputFilePath) override;
    void OnRecordSessionAborted() override;
    void OnRecordSessionError(const std::string& message) override;

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
    void onTick();

private:
    void createFileReader();
    void releaseFileReader();
    void startPlayback(const QString& filePath);
    void stopRecording(bool promptForSavePath = false);
    void updateProgress();
    void appendDebugMessage(const QString& message);
    void refreshNetworkPanel();

    ClickableSlider* m_progressSlider = nullptr;
    QWidget* m_sidePanel = nullptr;
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
    QString m_currentFilePath;
    bool m_isPlaying{false};
    bool m_hasOpenedFile{false};
    float m_durationSeconds{0.0f};
    QStringList m_debugMessages;
    av::NetworkSession m_networkSession;
    av::AVSynchronizer m_synchronizer;
    av::FileReader* m_fileReader{nullptr};
    std::unique_ptr<av::IRecordSession> m_recordSession;
};

#endif //MAINWINDOW_H
