//
// Created by Joe on 26-3-3.
//

#include "MainWindow.h"

#include <algorithm>
#include <QDir>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QStackedWidget>

#include "../AV/src/Reader/FileReader.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_aiAssistantService(*this) {
    m_synchronizer.Init();
    m_recordSession.reset(av::IRecordSession::Create());
    if (m_recordSession) {
        m_recordSession->SetListener(this);
    }

    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    centralWidget->setObjectName("mainCentral");
    setStyleSheet(R"(
        QWidget#mainCentral {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 #0b1220, stop:0.60 #111827, stop:1 #172033);
        }
        QLabel {
            color: #dbe7ff;
        }
        QLineEdit, QSlider, QPushButton {
            font-size: 13px;
        }
        QLineEdit {
            background: rgba(10, 16, 28, 0.92);
            color: #e6eefc;
            border: 1px solid rgba(148, 163, 184, 0.35);
            border-radius: 10px;
            padding: 10px 12px;
        }
        QPushButton {
            background: rgba(31, 41, 55, 0.92);
            color: #eff6ff;
            border: 1px solid rgba(148, 163, 184, 0.32);
            border-radius: 10px;
            padding: 10px 14px;
        }
        QPushButton:hover {
            background: rgba(51, 65, 85, 0.98);
        }
        QSlider::groove:horizontal {
            background: rgba(71, 85, 105, 0.55);
            height: 6px;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #f8fafc;
            width: 16px;
            margin: -6px 0;
            border-radius: 8px;
        }
    )");

    auto* vbox = new QVBoxLayout(centralWidget);
    vbox->setContentsMargins(14, 14, 14, 14);
    vbox->setSpacing(10);

    auto* topLayout = new QHBoxLayout();
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    m_playerWidget = new PlayerWidget(this);
    m_playerWidget->setStyleSheet("background: #000; border-radius: 18px;");
    topLayout->addWidget(m_playerWidget, 5);
    m_synchronizer.SetVideoOutput(m_playerWidget);
    m_playerWidget->SetMetricsSources(
        [this] { return m_synchronizer.GetAudioClock(); },
        [this] { return m_recordSession && m_recordSession->IsRecording(); });

    m_sidePanel = new QWidget(this);
    m_sidePanel->setMinimumWidth(360);
    m_sidePanel->setStyleSheet("background: rgba(7, 12, 22, 0.78); border: 1px solid rgba(148, 163, 184, 0.20); border-radius: 18px;");
    auto* sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(16, 16, 16, 16);
    sideLayout->setSpacing(10);

    auto* panelTabs = new QHBoxLayout();
    auto* networkTab = new QPushButton("网络", m_sidePanel);
    auto* aiTab = new QPushButton("AI 助手", m_sidePanel);
    networkTab->setStyleSheet("QPushButton { padding: 7px 10px; border-radius: 9px; color: #aab8cc; background: rgba(15, 23, 42, 0.55); border: 1px solid transparent; } QPushButton:checked { color: white; background: #315efb; }");
    aiTab->setStyleSheet(networkTab->styleSheet());
    networkTab->setCheckable(true);
    aiTab->setCheckable(true);
    networkTab->setChecked(true);
    panelTabs->addWidget(networkTab);
    panelTabs->addWidget(aiTab);
    sideLayout->addLayout(panelTabs);

    auto* panelStack = new QStackedWidget(m_sidePanel);
    auto* networkPage = new QWidget(panelStack);
    auto* networkLayout = new QVBoxLayout(networkPage);
    networkLayout->setContentsMargins(0, 0, 0, 0);
    networkLayout->setSpacing(10);
    m_aiAssistantPanel = new AIAssistantPanel(panelStack);
    panelStack->addWidget(networkPage);
    panelStack->addWidget(m_aiAssistantPanel);
    sideLayout->addWidget(panelStack, 1);
    connect(networkTab, &QPushButton::clicked, this, [panelStack, networkTab, aiTab] {
        panelStack->setCurrentIndex(0);
        networkTab->setChecked(true);
        aiTab->setChecked(false);
    });
    connect(aiTab, &QPushButton::clicked, this, [panelStack, networkTab, aiTab] {
        panelStack->setCurrentIndex(1);
        networkTab->setChecked(false);
        aiTab->setChecked(true);
    });
    connect(m_aiAssistantPanel, &AIAssistantPanel::playPauseRequested, this, [this] {
        m_aiAssistantService.TogglePlayback();
    });
    connect(m_aiAssistantPanel, &AIAssistantPanel::seekToStartRequested, this, [this] {
        m_aiAssistantService.SeekToStart();
    });

    auto* panelTitle = new QLabel("网络控制台", m_sidePanel);
    QFont panelTitleFont;
    panelTitleFont.setPointSize(18);
    panelTitleFont.setBold(true);
    panelTitle->setFont(panelTitleFont);
    panelTitle->setStyleSheet("color: #f8fafc;");

    auto* sourceTitle = new QLabel("当前播放地址", m_sidePanel);
    m_currentSourceValueLabel = new QLabel("未加载", m_sidePanel);
    m_currentSourceValueLabel->setWordWrap(true);
    m_currentSourceValueLabel->setStyleSheet("color: #93c5fd;");

    auto* networkStateTitle = new QLabel("网络连接状态", m_sidePanel);
    m_networkStatusValueLabel = new QLabel("空闲", m_sidePanel);
    m_networkStatusValueLabel->setStyleSheet("color: #86efac;");

    auto* errorTitle = new QLabel("最近错误信息", m_sidePanel);
    m_errorValueLabel = new QLabel("暂无错误", m_sidePanel);
    m_errorValueLabel->setWordWrap(true);
    m_errorValueLabel->setMinimumHeight(66);
    m_errorValueLabel->setStyleSheet("background: rgba(127, 29, 29, 0.35); color: #fecaca; border-radius: 12px; padding: 10px;");

    auto* networkTitle = new QLabel("网络地址 / 本地地址", m_sidePanel);
    m_networkUrlEdit = new QLineEdit(m_sidePanel);
    m_networkUrlEdit->setPlaceholderText("输入本地路径或网络地址，例如 http:// / rtmp:// / rtsp://");
    m_networkPlayButton = new QPushButton("连接并播放", m_sidePanel);
    m_networkPlayButton->setStyleSheet("background: rgba(22, 101, 52, 0.92); color: white; border-radius: 10px; padding: 10px 14px;");
    m_networkStopButton = new QPushButton("停止网络源", m_sidePanel);
    m_networkStopButton->setStyleSheet("background: rgba(127, 29, 29, 0.92); color: white; border-radius: 10px; padding: 10px 14px;");

    connect(m_networkPlayButton, &QPushButton::clicked, this, &MainWindow::onNetworkPlayRequested);
    connect(m_networkStopButton, &QPushButton::clicked, this, &MainWindow::onNetworkStopRequested);
    connect(m_networkUrlEdit, &QLineEdit::returnPressed, this, &MainWindow::onNetworkPlayRequested);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addWidget(m_networkPlayButton, 2);
    buttonRow->addWidget(m_networkStopButton, 1);

    auto* debugTitle = new QLabel("调试摘要", m_sidePanel);
    m_debugLabel = new QLabel("等待网络操作...", m_sidePanel);
    m_debugLabel->setWordWrap(true);
    m_debugLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_debugLabel->setMinimumHeight(220);
    m_debugLabel->setStyleSheet("background: rgba(6, 12, 22, 0.88); color: #d7f7d7; border: 1px solid rgba(148, 163, 184, 0.20); border-radius: 12px; padding: 10px;");

    networkLayout->addWidget(panelTitle);
    networkLayout->addSpacing(4);
    networkLayout->addWidget(sourceTitle);
    networkLayout->addWidget(m_currentSourceValueLabel);
    networkLayout->addSpacing(4);
    networkLayout->addWidget(networkStateTitle);
    networkLayout->addWidget(m_networkStatusValueLabel);
    networkLayout->addSpacing(4);
    networkLayout->addWidget(errorTitle);
    networkLayout->addWidget(m_errorValueLabel);
    networkLayout->addSpacing(6);
    networkLayout->addWidget(networkTitle);
    networkLayout->addWidget(m_networkUrlEdit);
    networkLayout->addLayout(buttonRow);
    networkLayout->addWidget(debugTitle);
    networkLayout->addWidget(m_debugLabel, 1);

    topLayout->addWidget(m_sidePanel, 2);
    vbox->addLayout(topLayout, 9);

    m_progressSlider = new ClickableSlider(Qt::Horizontal, this);
    m_progressSlider->setRange(0, 1000);
    connect(m_progressSlider, &QSlider::sliderMoved, this, &MainWindow::onSliderMoved);
    vbox->addWidget(m_progressSlider);

    m_controllerWidget = new ControllerWidget(this);
    vbox->addWidget(m_controllerWidget);
    connect(m_controllerWidget, &ControllerWidget::importRequested, this, &MainWindow::onImportRequested);
    connect(m_controllerWidget, &ControllerWidget::playToggled, this, &MainWindow::onPlayToggled);
    connect(m_controllerWidget, &ControllerWidget::recordToggled, this, &MainWindow::onRecordToggled);
    connect(m_controllerWidget, &ControllerWidget::flipFilterToggled, m_playerWidget, &PlayerWidget::SetFlipEnabled);
    connect(m_controllerWidget, &ControllerWidget::grayFilterToggled, m_playerWidget, &PlayerWidget::SetGrayEnabled);
    connect(m_controllerWidget, &ControllerWidget::invertFilterToggled, m_playerWidget, &PlayerWidget::SetInvertEnabled);
    connect(m_controllerWidget, &ControllerWidget::stickerFilterToggled, m_playerWidget, &PlayerWidget::SetStickerEnabled);
    connect(m_controllerWidget, &ControllerWidget::stickerAssetSwitchRequested, m_playerWidget, &PlayerWidget::CycleStickerResource);

    setMinimumSize(1280, 720);
    resize(1440, 900);
    setWindowTitle("AVPlayer");

    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout, this, &MainWindow::onTick);
    m_renderTimer.start();

    refreshNetworkPanel();
    syncAiContext();
    appendDebugMessage("Qt 网络框架已初始化，等待输入地址或导入本地文件。");
}

MainWindow::~MainWindow() {
    stopRecording(false);
    releaseFileReader();
    m_synchronizer.Shutdown();
}

void MainWindow::OnFileReaderNotifyAudioSamples(std::shared_ptr<av::IAudioSamples> audioSamples) {
    static int audioSampleCount = 0;
    ++audioSampleCount;
    if (audioSampleCount <= 3 || audioSampleCount % 200 == 0) {
        std::cout << "[MainWindow] audio samples #" << audioSampleCount
                  << " pts=" << audioSamples->pts
                  << " ts=" << audioSamples->GetTimeStamp()
                  << " rate=" << audioSamples->sampleRate
                  << " channels=" << audioSamples->channels << std::endl;
    }
    if (m_recordSession && m_recordSession->IsRecording()) {
        m_recordSession->WriteAudioSamples(audioSamples);
    }
    // QAudioSink belongs to the UI thread. Decoder callbacks run on a worker thread.
    const uint64_t generation = m_playbackGeneration.load();
    QMetaObject::invokeMethod(this, [this, audioSamples, generation] {
        if (generation == m_playbackGeneration.load()) {
            m_synchronizer.PushAudioSamples(audioSamples);
        }
    }, Qt::QueuedConnection);
}

void MainWindow::OnFileReaderNotifyVideoFrame(std::shared_ptr<av::IVideoFrame> videoFrame) {
    static int videoFrameCount = 0;
    ++videoFrameCount;
    if (videoFrameCount <= 5 || videoFrameCount % 120 == 0 || (videoFrame->flags & AVFrameFlag::KFlush)) {
        std::cout << "[MainWindow] video frames #" << videoFrameCount
                  << " pts=" << videoFrame->pts
                  << " ts=" << videoFrame->GetTimeStamp()
                  << " size=" << videoFrame->width << "x" << videoFrame->height
                  << " flags=" << videoFrame->flags
                  << " hasData=" << (videoFrame->data ? 1 : 0) << std::endl;
    }
    if (m_recordSession && m_recordSession->IsRecording()) {
        m_recordSession->WriteVideoFrame(videoFrame);
    }
    m_synchronizer.PushVideoFrame(videoFrame);
}

void MainWindow::OnFileReaderNotifyAudioFinished() {
    std::cout << "[MainWindow] audio stream finished" << std::endl;
}

void MainWindow::OnFileReaderNotifyVideoFinished() {
    std::cout << "[MainWindow] video stream finished" << std::endl;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    m_renderTimer.stop();
    stopRecording(false);
    releaseFileReader();
    m_synchronizer.Shutdown();
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent * event) {}
void MainWindow::mousePressEvent(QMouseEvent *event) {}
void MainWindow::mouseMoveEvent(QMouseEvent *event) {}
void MainWindow::mouseReleaseEvent(QMouseEvent *event) {}
void MainWindow::mouseDoubleClickEvent(QMouseEvent *event) {}
void MainWindow::onSliderMoved(int value) {
    if (!m_fileReader || m_durationSeconds <= 0.0f || !m_progressSlider) {
        return;
    }
    const float position = static_cast<float>(value) / static_cast<float>(m_progressSlider->maximum());
    std::cout << "[MainWindow] seek slider value=" << value
              << " progress=" << position << std::endl;
    m_fileReader->SeekTo(position);
}

void MainWindow::onImportRequested(const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }
    m_currentFilePath = filePath;
    m_networkSession.Start(filePath.toStdString());
    if (m_networkUrlEdit) {
        m_networkUrlEdit->setText(filePath);
    }
    refreshNetworkPanel();
    appendDebugMessage(QString("导入地址：%1").arg(filePath));
    startPlayback(filePath);
}

void MainWindow::onNetworkPlayRequested() {
    if (!m_networkUrlEdit) {
        return;
    }
    const QString url = m_networkUrlEdit->text().trimmed();
    if (url.isEmpty()) {
        m_networkSession.SetError("网络地址为空，无法播放。");
        refreshNetworkPanel();
        appendDebugMessage("网络地址为空，无法播放。");
        return;
    }
    if (!av::NetworkSession::IsSupportedUrl(url.toStdString())) {
        m_networkSession.SetError("暂不支持该地址格式。");
        refreshNetworkPanel();
        appendDebugMessage(QString("不支持的地址：%1").arg(url));
        return;
    }

    m_networkSession.Start(url.toStdString());
    m_currentFilePath = url;
    refreshNetworkPanel();
    appendDebugMessage(QString("提交播放地址：%1").arg(url));
    startPlayback(url);
}

void MainWindow::onNetworkStopRequested() {
    m_networkSession.Stop();
    refreshNetworkPanel();
    appendDebugMessage("停止网络源/播放");
    releaseFileReader();
    m_synchronizer.Stop();
    syncAiContext();
}

void MainWindow::onPlayToggled(bool playing) {
    if (playing) {
        if (!m_hasOpenedFile) {
            if (m_currentFilePath.isEmpty()) {
                if (m_controllerWidget) {
                    m_controllerWidget->SetPlaying(false);
                }
                m_isPlaying = false;
                return;
            }
            startPlayback(m_currentFilePath);
        } else if (m_fileReader) {
            m_fileReader->Start();
            m_isPlaying = true;
        }
        if (m_controllerWidget) m_controllerWidget->SetPlaying(m_isPlaying);
        syncAiContext();
        return;
    }

    if (m_fileReader) {
        m_fileReader->Paused();
    }
    m_isPlaying = false;
    if (m_controllerWidget) m_controllerWidget->SetPlaying(false);
    syncAiContext();
    m_networkSession.Stop();
    refreshNetworkPanel();
}

void MainWindow::onRecordToggled(bool recording) {
    if (!m_controllerWidget) {
        return;
    }

    if (!recording) {
        stopRecording(true);
        syncAiContext();
        return;
    }

    if (!m_hasOpenedFile || !m_fileReader || !m_recordSession) {
        appendDebugMessage("当前没有可录制的播放内容。");
        m_controllerWidget->SetRecording(false);
        return;
    }

    av::RecorderConfig config;
    config.width = m_fileReader->GetVideoWidth();
    config.height = m_fileReader->GetVideoHeight();
    config.audioChannels = 2;
    config.audioSampleRate = 44100;
    if (config.width <= 0 || config.height <= 0) {
        appendDebugMessage("录制启动失败：视频尺寸无效。");
        m_controllerWidget->SetRecording(false);
        return;
    }

    if (!m_recordSession->Start(config)) {
        appendDebugMessage("录制启动失败：无法创建录制会话。");
        m_controllerWidget->SetRecording(false);
        return;
    }

    m_controllerWidget->SetRecording(true);
    syncAiContext();
    appendDebugMessage("录制会话已启动，停止时将询问保存位置。");
}

void MainWindow::onTick() {
    m_synchronizer.Tick();
    updateProgress();
}

void MainWindow::StartBenchmark(const QString& filePath, int seconds, bool sticker,
                                bool record, const QString& outputPath, const QString& capturePath) {
    m_playerWidget->SetStickerEnabled(sticker);
    startPlayback(filePath);
    if (record && m_hasOpenedFile) onRecordToggled(true);
    if (!capturePath.isEmpty()) {
        QTimer::singleShot(2000, this, [this, capturePath] {
            const bool saved = m_playerWidget->grabFramebuffer().save(capturePath);
            std::cout << "[PlaybackMetrics] captured_frame=" << capturePath.toStdString()
                      << " saved=" << (saved ? 1 : 0) << std::endl;
        });
    }
    QTimer::singleShot(std::max(1, seconds) * 1000, this, [this, record, outputPath] {
        m_playerWidget->FlushMetrics();
        if (record && m_recordSession && m_recordSession->IsRecording()) {
            m_recordSession->StopAndSave(outputPath.toStdString());
        }
        close();
    });
}

void MainWindow::createFileReader() {
    if (m_fileReader) {
        return;
    }
    m_fileReader = new av::FileReader();
    m_fileReader->SetListener(this);
}

void MainWindow::TogglePlayback() {
    onPlayToggled(!m_isPlaying);
}

bool MainWindow::SeekToStart() {
    if (!m_fileReader || !m_hasOpenedFile) {
        appendDebugMessage("AI 助手：当前没有可跳转的视频。");
        return false;
    }
    m_fileReader->SeekTo(0.0f);
    appendDebugMessage("AI 助手：已请求跳转到视频开头。");
    return true;
}

void MainWindow::releaseFileReader() {
    ++m_playbackGeneration;
    stopRecording(false);
    if (!m_fileReader) {
        syncAiContext();
        return;
    }
    m_fileReader->Stop();
    delete m_fileReader;
    m_fileReader = nullptr;
    m_hasOpenedFile = false;
    m_isPlaying = false;
    m_durationSeconds = 0.0f;
    syncAiContext();
    if (m_progressSlider) {
        m_progressSlider->setValue(0);
    }
    refreshNetworkPanel();
    if (m_controllerWidget) {
        m_controllerWidget->SetPlaying(false);
        m_controllerWidget->SetRecording(false);
        m_controllerWidget->SetRecordEnabled(false);
    }
}

void MainWindow::startPlayback(const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    m_currentFilePath = filePath;
    releaseFileReader();
    m_synchronizer.Stop();
    createFileReader();

    std::string path = filePath.toStdString();
    if (!m_fileReader->Open(path)) {
        std::cout << "[MainWindow] failed to open file: " << path << std::endl;
        m_networkSession.SetError(QString("打开失败：%1").arg(filePath).toStdString());
        refreshNetworkPanel();
        appendDebugMessage(QString("打开失败：%1").arg(filePath));
        return;
    }

    m_durationSeconds = m_fileReader->GetDuration();
    std::cout << "[MainWindow] playback opened path=" << path
              << " duration=" << m_durationSeconds
              << " width=" << m_fileReader->GetVideoWidth()
              << " height=" << m_fileReader->GetVideoHeight() << std::endl;

    m_fileReader->Start();
    std::cout << "[MainWindow] playback started" << std::endl;
    m_networkSession.Start(path);
    refreshNetworkPanel();
    m_hasOpenedFile = true;
    m_isPlaying = true;
    if (m_controllerWidget) {
        m_controllerWidget->SetPlaying(true);
        m_controllerWidget->SetRecordEnabled(true);
    }
    syncAiContext();
}

void MainWindow::stopRecording(bool promptForSavePath) {
    if (m_controllerWidget) {
        m_controllerWidget->SetRecording(false);
    }
    if (!m_recordSession || !m_recordSession->IsRecording()) {
        syncAiContext();
        return;
    }

    if (!promptForSavePath) {
        m_recordSession->Abort();
        syncAiContext();
        return;
    }

    const QString defaultPath = QString::fromStdString(
        m_recordSession->BuildDefaultOutputPath(QDir::currentPath().toStdString()));
    const QString outputPath = QFileDialog::getSaveFileName(
        this, "保存录制文件", defaultPath, "MP4 文件 (*.mp4)");
    if (outputPath.isEmpty()) {
        m_recordSession->Abort();
        syncAiContext();
        return;
    }

    if (!m_recordSession->StopAndSave(outputPath.toStdString())) {
        appendDebugMessage("录制保存失败。");
    }
    syncAiContext();
}
/*
 * 同步主界面相关信息给ui界面，这样可以用于ai的回复
 */
void MainWindow::syncAiContext() {
    const bool loaded = m_hasOpenedFile && m_fileReader;
    const bool recording = m_recordSession && m_recordSession->IsRecording();
    const int width = loaded ? m_fileReader->GetVideoWidth() : 0;
    const int height = loaded ? m_fileReader->GetVideoHeight() : 0;
    m_aiAssistantService.UpdateMediaContext({loaded ? m_currentFilePath.toStdString() : std::string{},
        loaded, loaded && m_isPlaying, recording, width, height});
    if (m_aiAssistantPanel) {
        m_aiAssistantPanel->SetMediaContext(loaded ? m_currentFilePath : QString{}, loaded && m_isPlaying,
            width, height);
        m_aiAssistantPanel->SetRecording(recording);
    }
}

void MainWindow::OnRecordSessionStarted(const std::string& tempFilePath) {
    appendDebugMessage(QString("录制已开始，临时文件：%1").arg(QString::fromStdString(tempFilePath)));
}

void MainWindow::OnRecordSessionSaved(const std::string& outputFilePath) {
    appendDebugMessage(QString("录制已保存：%1").arg(QString::fromStdString(outputFilePath)));
}

void MainWindow::OnRecordSessionAborted() {
    appendDebugMessage("录制已中止，临时文件已清理。");
}

void MainWindow::OnRecordSessionError(const std::string& message) {
    appendDebugMessage(QString::fromStdString(message));
}

void MainWindow::updateProgress() {
    if (!m_progressSlider || m_progressSlider->isSliderDown() || m_durationSeconds <= 0.0f) {
        return;
    }

    const double masterClock = m_synchronizer.GetMasterClock();
    if (masterClock < 0.0) {
        return;
    }

    const double progress = std::clamp(masterClock / static_cast<double>(m_durationSeconds), 0.0, 1.0);
    const int sliderValue = static_cast<int>(progress * m_progressSlider->maximum());
    m_progressSlider->setValue(sliderValue);
}

void MainWindow::appendDebugMessage(const QString& message) {
    if (message.isEmpty()) {
        return;
    }
    qDebug().noquote() << "[UI]" << message;
    m_debugMessages.append(message);
    while (m_debugMessages.size() > 8) {
        m_debugMessages.removeFirst();
    }
    if (m_debugLabel) {
        m_debugLabel->setText(m_debugMessages.join("\n"));
    }
}

void MainWindow::refreshNetworkPanel() {
    if (m_currentSourceValueLabel) {
        const std::string currentUrl = m_networkSession.GetUrl();
        m_currentSourceValueLabel->setText(currentUrl.empty() ? "未加载" : QString::fromStdString(currentUrl));
    }
    if (m_networkStatusValueLabel) {
        QString status = QString::fromStdString(m_networkSession.GetStateText());
        const std::string url = m_networkSession.GetUrl();
        if (!url.empty()) {
            status = QString("%1 / %2")
                             .arg(QString::fromStdString(av::NetworkSession::DescribeSource(url)))
                             .arg(status);
        }
        m_networkStatusValueLabel->setText(status);
    }
    if (m_errorValueLabel) {
        const std::string errorText = m_networkSession.GetLastError();
        m_errorValueLabel->setText(errorText.empty() ? "暂无错误" : QString::fromStdString(errorText));
    }
}
