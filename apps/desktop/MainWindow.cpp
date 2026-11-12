//
// Created by Joe on 26-3-3.
//

#include "MainWindow.h"

#include <algorithm>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QStackedWidget>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "Reader/FileReader.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_aiAssistantService(*this), m_agentBridge(this) {
    m_synchronizer.Init();
    m_streamingMode = qEnvironmentVariableIntValue("AVPLAYER_STREAMING_MODE") != 0;
    if (const auto env = qEnvironmentVariable("AVPLAYER_MEDIA_SERVER"); !env.isEmpty())
        m_mediaServerAddress = env;
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

    QString logoPath;
#ifdef AVPLAYER_PROJECT_ROOT
    logoPath = QDir(QStringLiteral(AVPLAYER_PROJECT_ROOT)).filePath("resource/avplayer-logo.svg");
#endif
    if (!QFileInfo::exists(logoPath)) logoPath = QDir::current().filePath("resource/avplayer-logo.svg");
    const QString logoPngPath = QDir(QFileInfo(logoPath).path()).filePath("avplayer-logo.png");
    const QIcon appIcon(QFileInfo::exists(logoPngPath) ? logoPngPath : logoPath);
    if (!appIcon.isNull()) {
        setWindowIcon(appIcon);
        QApplication::setWindowIcon(appIcon);
    }

    auto* brandHeader = new QHBoxLayout();
    brandHeader->setContentsMargins(2, 0, 2, 0);
    brandHeader->setSpacing(10);
    auto* brandIcon = new QLabel(centralWidget);
    brandIcon->setFixedSize(38, 38);
    brandIcon->setAlignment(Qt::AlignCenter);
    if (QFileInfo::exists(logoPngPath)) {
        QPixmap rendered(38, 38);
        rendered = QPixmap(logoPngPath).scaled(38, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        brandIcon->setPixmap(rendered);
    }
    auto* brandText = new QVBoxLayout();
    brandText->setSpacing(0);
    auto* brandTitle = new QLabel("AVPlayer", centralWidget);
    brandTitle->setStyleSheet("color: #f5f8ff; font-size: 17px; font-weight: 800;");
    auto* brandSubtitle = new QLabel("AI Media Studio", centralWidget);
    brandSubtitle->setStyleSheet("color: #8fa8c8; font-size: 10px; letter-spacing: 1px;");
    brandText->addWidget(brandTitle);
    brandText->addWidget(brandSubtitle);
    brandHeader->addWidget(brandIcon);
    brandHeader->addLayout(brandText);
    brandHeader->addStretch(1);
    vbox->addLayout(brandHeader);

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
    auto* agentTab = new QPushButton("Agent", m_sidePanel);
    networkTab->setStyleSheet("QPushButton { padding: 7px 8px; border-radius: 8px; color: #aab8cc; background: rgba(15, 23, 42, 0.55); border: 1px solid transparent; } QPushButton:hover:enabled { color: white; background: #1d3850; } QPushButton:checked { color: white; background: #315efb; }");
    aiTab->setStyleSheet(networkTab->styleSheet());
    agentTab->setStyleSheet(networkTab->styleSheet());
    networkTab->setCheckable(true);
    aiTab->setCheckable(true);
    agentTab->setCheckable(true);
    networkTab->setChecked(true);
    panelTabs->addWidget(networkTab);
    panelTabs->addWidget(aiTab);
    panelTabs->addWidget(agentTab);
    sideLayout->addLayout(panelTabs);

    auto* panelStack = new QStackedWidget(m_sidePanel);
    auto* networkPage = new QWidget(panelStack);
    auto* networkLayout = new QVBoxLayout(networkPage);
    networkLayout->setContentsMargins(0, 0, 0, 0);
    networkLayout->setSpacing(10);
    m_aiAssistantPanel = new AIAssistantPanel(panelStack);
    m_agentPanel = new AgentPanel(panelStack);
    m_aiProvider = QSettings("AVPlayer", "AVPlayer").value("ai/provider", "local").toString() == "cloud"
        ? "cloud" : "local";
    m_aiAssistantPanel->SetProviderMode(m_aiProvider);
    m_agentPanel->SetProviderMode(m_aiProvider);
    panelStack->addWidget(networkPage);
    panelStack->addWidget(m_aiAssistantPanel);
    panelStack->addWidget(m_agentPanel);
    sideLayout->addWidget(panelStack, 1);
    connect(networkTab, &QPushButton::clicked, this, [panelStack, networkTab, aiTab, agentTab] {
        panelStack->setCurrentIndex(0);
        networkTab->setChecked(true);
        aiTab->setChecked(false);
        agentTab->setChecked(false);
    });
    connect(aiTab, &QPushButton::clicked, this, [panelStack, networkTab, aiTab, agentTab] {
        panelStack->setCurrentIndex(1);
        networkTab->setChecked(false);
        aiTab->setChecked(true);
        agentTab->setChecked(false);
    });
    connect(agentTab, &QPushButton::clicked, this, [panelStack, networkTab, aiTab, agentTab] {
        panelStack->setCurrentIndex(2);
        networkTab->setChecked(false);
        aiTab->setChecked(false);
        agentTab->setChecked(true);
    });
    setupAgentRuntime();
    connect(m_agentPanel, &AgentPanel::goalSubmitted, this, [this](const QString& goal) {
        if (!m_agentBridge.IsRunning() && !m_agentBridge.Start()) return;
        if (m_desktopMediaId.isEmpty()) {
            if (m_currentFilePath.isEmpty()) {
                m_agentPanel->SetRuntimeStatus("请先打开一个本地视频。", false);
                return;
            }
            m_pendingGoal = goal;
            m_agentPanel->ResetExecution();
            m_agentPanel->SetRuntimeStatus("正在上传媒体并提交任务…", true);
            if (!m_desktopUploadInProgress) {
                startDesktopUpload(m_currentFilePath, /*connectWhenReady=*/m_streamingMode);
            }
            return;
        }
        m_agentPanel->ResetExecution();
        m_agentPanel->SetRuntimeStatus("正在提交任务…", true);
        m_agentBridge.SubmitGoal(goal, "desktop");
    });
    connect(m_agentPanel, &AgentPanel::stopRequested, this, [this] {
        m_pendingGoal.clear();
        m_agentBridge.Stop();
        m_agentPanel->SetRuntimeStatus("任务已停止，可重新提交目标。", false);
    });
    connect(m_aiAssistantPanel, &AIAssistantPanel::playPauseRequested, this, [this] {
        m_aiAssistantService.TogglePlayback();
    });
    connect(m_aiAssistantPanel, &AIAssistantPanel::seekToStartRequested, this, [this] {
        m_aiAssistantService.SeekToStart();
    });
    connect(m_aiAssistantPanel, &AIAssistantPanel::subtitleVisibilityChanged,
            m_playerWidget, &PlayerWidget::SetSubtitlesEnabled);
    m_aiAssistantPanel->SetConfiguration(m_aiAssistantService.IsModelConfigured(), m_aiAssistantService.IsSpeechConfigured(), m_aiAssistantService.ConfigurationStatus());
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::modelTaskRequested, this,
            [this](const QString& task, const QJsonObject& arguments) {
        submitAiTask(task, arguments);
    });
    const auto changeProvider = [this](const QString& provider) {
        m_aiProvider = provider == "cloud" ? "cloud" : "local";
        QSettings("AVPlayer", "AVPlayer").setValue("ai/provider", m_aiProvider);
        m_agentBridge.SetDefaultProvider(m_aiProvider);
        m_aiAssistantPanel->SetProviderMode(m_aiProvider);
        m_agentPanel->SetProviderMode(m_aiProvider);
        if (m_agentBridge.IsRunning()) m_agentBridge.SetProvider(m_aiProvider);
    };
    connect(m_aiAssistantPanel, &AIAssistantPanel::providerModeChanged, this, changeProvider);
    connect(m_agentPanel, &AgentPanel::providerModeChanged, this, changeProvider);
    connect(m_aiAssistantPanel, &AIAssistantPanel::transcriptionRequested, &m_aiAssistantService, &av::ai::AIAssistantService::RequestTranscription);
    connect(m_aiAssistantPanel, &AIAssistantPanel::summaryRequested, &m_aiAssistantService, &av::ai::AIAssistantService::RequestSummary);
    connect(m_aiAssistantPanel, &AIAssistantPanel::searchRequested, &m_aiAssistantService, &av::ai::AIAssistantService::RequestSemanticSearch);
    connect(m_aiAssistantPanel, &AIAssistantPanel::editPlanRequested, &m_aiAssistantService, &av::ai::AIAssistantService::RequestEditPlan);
    connect(m_aiAssistantPanel, &AIAssistantPanel::exportEditPlanRequested, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "选择剪辑导出目录");
        if (!dir.isEmpty()) m_aiAssistantService.ExportEditPlan(dir);
    });
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::transcriptLoaded, m_aiAssistantPanel, &AIAssistantPanel::SetTranscript);
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::transcriptLoaded, m_playerWidget, [this](const QString&) {
        m_playerWidget->SetSubtitleSegments(m_aiAssistantService.TranscriptSegments());
        m_aiAssistantPanel->SetSubtitleAvailable(!m_aiAssistantService.TranscriptSegments().isEmpty());
    });
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::taskStarted, m_aiAssistantPanel, &AIAssistantPanel::SetTaskStarted);
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::taskProgress, m_aiAssistantPanel, &AIAssistantPanel::SetTaskProgress);
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::taskFinished, m_aiAssistantPanel, &AIAssistantPanel::SetTaskResult);
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::taskFailed, m_aiAssistantPanel, &AIAssistantPanel::SetTaskError);
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::editPlanReady, m_aiAssistantPanel, &AIAssistantPanel::SetEditPlanReady);

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
    connect(m_controllerWidget, &ControllerWidget::screenshotRequested, this, &MainWindow::onScreenshotRequested);
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
    m_streamStatusTimer.setInterval(500);
    connect(&m_streamStatusTimer, &QTimer::timeout, this, &MainWindow::updateStreamingStatus);
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
    if (!m_progressSlider) return;
    if (m_streamingMode) {
        if (!m_streamingSource) return;
        const float position = static_cast<float>(value) / static_cast<float>(m_progressSlider->maximum());
        std::string error;
        if (!m_streamingSource->Control("seek", position, nullptr, &error))
            appendDebugMessage("拖动进度条失败：" + QString::fromStdString(error));
        return;
    }
    if (!m_fileReader || m_durationSeconds <= 0.0f) {
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
        } else if (m_streamingMode && m_streamingSource) {
            std::string error;
            if (!m_streamingSource->Control("play", 0.0f, nullptr, &error))
                appendDebugMessage("恢复播放失败：" + QString::fromStdString(error));
            m_isPlaying = true;
        } else if (m_fileReader) {
            m_fileReader->Start();
            m_isPlaying = true;
        }
        if (m_controllerWidget) m_controllerWidget->SetPlaying(m_isPlaying);
        syncAiContext();
        return;
    }

    if (m_streamingMode && m_streamingSource) {
        std::string error;
        if (!m_streamingSource->Control("pause", 0.0f, nullptr, &error))
            appendDebugMessage("暂停失败：" + QString::fromStdString(error));
    } else if (m_fileReader) {
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

    if (m_streamingMode && !m_streamingSource) {
        // The WebRTC source may still be negotiating (upload + connect).
        m_pendingStreamingRecord = recording;
        m_controllerWidget->SetRecording(recording);
        appendDebugMessage(recording ? "连接完成后开始服务端录制。" : "已取消待开始的录制。");
        return;
    }

    if (m_streamingMode && m_streamingSource) {
        av::WebRtcPlaybackSource::PlaybackStatus status;
        std::string error;
        if (!m_streamingSource->Control("record_start", 0.0f, &status, &error)) {
            appendDebugMessage("服务端录制启动失败：" + QString::fromStdString(error));
            m_controllerWidget->SetRecording(false);
            return;
        }
        m_controllerWidget->SetRecording(true);
        appendDebugMessage("服务端录制已启动，停止时保存到共享存储。");
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

void MainWindow::onScreenshotRequested() {
    if (!m_hasOpenedFile || !m_playerWidget) return;
    if (!m_playerWidget->HasPresentedFrame()) {
        QMessageBox::warning(this, "截图失败", "当前画面尚未准备好，请稍后重试。");
        return;
    }

    const QImage image = m_playerWidget->grabFramebuffer();
    if (image.isNull()) { QMessageBox::warning(this, "截图失败", "当前画面尚未准备好，请稍后重试。"); return; }

    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString directory = pictures.isEmpty() || !QDir(pictures).exists() ? QDir::homePath() : pictures;
    const QString basename = QFileInfo(m_currentFilePath).completeBaseName();
    const QString suggestedPath = QDir(directory).filePath(
        QString("%1_%2.png").arg(basename.isEmpty() ? "AVPlayer" : basename,
                                QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    QFileDialog dialog(this, "保存当前画面", suggestedPath, "PNG 图片 (*.png)");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("png");
    if (dialog.exec() != QDialog::Accepted) return;

    const QString path = dialog.selectedFiles().constFirst();
    if (!image.save(path, "PNG")) {
        QMessageBox::warning(this, "截图失败", "无法写入截图文件，请检查保存路径和权限。");
        return;
    }
    m_controllerWidget->ShowScreenshotSaved();
    appendDebugMessage("截图已保存：" + path);
}

bool MainWindow::SaveScreenshotToPath(const QString& path) {
    if (!m_playerWidget || !m_playerWidget->HasPresentedFrame()) return false;
    const QImage image = m_playerWidget->grabFramebuffer();
    return !image.isNull() && image.save(path, "PNG");
}

void MainWindow::setupAgentRuntime() {
    // services/agent 的默认监听地址；连接远程服务时通过 AVPLAYER_AGENT_SERVER 覆盖。
    QString serverAddress = QStringLiteral("localhost:50061");
    if (const auto env = qEnvironmentVariable("AVPLAYER_AGENT_SERVER"); !env.isEmpty())
        serverAddress = env;
    m_agentBridge.SetServerAddress(serverAddress);
    m_agentBridge.SetDefaultProvider(m_aiProvider);

    // get_media_context/get_transcript/extract_audio/save_transcript/set_edit_plan/
    // export_random_clip 完全由 services/agent 的 local_tools.py 执行（见 UploadMedia），
    // 不再作为 tool_call 回传给客户端；这里只保留必须读取/驱动本地播放器状态的工具。
    m_agentTools.Register({"player_control", "控制播放或回到开头",
        QJsonObject{{"type", "object"},
                    {"properties", QJsonObject{{"action", QJsonObject{{"type", "string"},
                        {"enum", QJsonArray{"play_pause", "seek_start"}}}}}},
                    {"required", QJsonArray{"action"}}}, [this](const QJsonObject& args) {
        const QString action = args.value("action").toString();
        if (action == "play_pause") return QJsonObject{{"success", m_aiAssistantService.TogglePlayback()}};
        if (action == "seek_start") return QJsonObject{{"success", m_aiAssistantService.SeekToStart()}};
        return QJsonObject{{"success", false}, {"error", "未知播放操作"}};
    }});
    m_agentTools.Register({"capture_screenshot", "保存当前播放器画面",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{{"output_path",
            QJsonObject{{"type", "string"}, {"description", "可选的 PNG 文件路径"}}}}}}, [this](const QJsonObject& args) {
        QString path = args.value("output_path").toString();
        if (path.isEmpty()) path = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath("avplayer-agent.png");
        return QJsonObject{{"success", SaveScreenshotToPath(path)}, {"path", path}};
    }});
    m_agentTools.Register({"export_clip", "导出已有智能剪辑建议；需要 output_dir 目录，不能用于随机导出。",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{{"output_dir", QJsonObject{{"type", "string"}}}}},
                    {"required", QJsonArray{"output_dir"}}},
        [](const QJsonObject&) { return QJsonObject{{"success", false}, {"error", "异步工具须经 MainWindow 调用。"}}; }});
    connect(&m_agentBridge, &av::agent::AgentBridge::runtimeStarted, this, [this] {
        m_agentPanel->SetRuntimeStatus("Runtime 已连接", true);
        m_agentBridge.SendToolCatalog(m_agentTools.Definitions());
        m_agentBridge.SetProvider(m_aiProvider);
    });
    connect(&m_agentBridge, &av::agent::AgentBridge::messageReceived, this, &MainWindow::handleAgentMessage);
    connect(&m_agentBridge, &av::agent::AgentBridge::protocolError, this, [this](const QString& error) {
        m_agentPanel->SetRuntimeStatus(error, false);
        m_agentPanel->SetAgentOutput(error);
        m_aiAssistantService.SetConfiguration(false, false, error);
        m_aiAssistantPanel->SetConfiguration(false, false, error);
    });
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::taskFinished, this, [this](const QString& task, const QString& result) {
        if (m_agentPendingRequestId.isEmpty()) return;
        const QHash<QString, QString> names{{"智能剪辑导出", "export_clip"}};
        if (names.value(task) != m_agentPendingTool) return;
        m_agentBridge.SendToolResult(m_agentPendingRequestId, m_agentPendingSessionId, m_agentPendingTool, QJsonObject{{"success", true}, {"result", result}});
        m_agentPendingRequestId.clear(); m_agentPendingSessionId.clear(); m_agentPendingTool.clear();
    });
    connect(&m_aiAssistantService, &av::ai::AIAssistantService::taskFailed, this, [this](const QString& task, const QString& error) {
        if (m_agentPendingRequestId.isEmpty() || task != "智能剪辑导出") return;
        m_agentBridge.SendToolResult(m_agentPendingRequestId, m_agentPendingSessionId, m_agentPendingTool, QJsonObject{{"success", false}, {"error", error}});
        m_agentPendingRequestId.clear(); m_agentPendingSessionId.clear(); m_agentPendingTool.clear();
    });
    m_agentBridge.Start();
}

void MainWindow::handleAgentMessage(const av::agent::AgentMessage& message) {
    if (message.type == "ready" || message.type == "provider_status") {
        const QString provider = message.payload.value("provider").toString();
        m_aiProvider = provider == "cloud" ? "cloud" : "local";
        m_agentBridge.SetDefaultProvider(m_aiProvider);
        QSettings("AVPlayer", "AVPlayer").setValue("ai/provider", m_aiProvider);
        m_aiAssistantPanel->SetProviderMode(m_aiProvider);
        m_agentPanel->SetProviderMode(m_aiProvider);
        const bool model = message.payload.value("model_configured").toBool();
        const bool speech = message.payload.value("speech_configured").toBool();
        const QString status = message.payload.value("config_status").toString();
        m_aiAssistantService.SetConfiguration(model, speech, status);
        m_aiAssistantPanel->SetConfiguration(model, speech, status);
        m_agentPanel->SetProviderStatus(m_aiProvider, model, status);
        return;
    }
    if (message.type == "task_result") {
        const QString task = message.payload.value("task").toString();
        const QHash<QString, QString> names{{"transcribe_subtitle", "字幕转写"}, {"summarize_video", "视频摘要"},
                                            {"semantic_search", "语义搜索"}, {"smart_edit", "智能剪辑"}};
        const bool success = message.payload.value("success").toBool();
        const QJsonArray segments = message.payload.value("segments").toArray();
        if (success && !segments.isEmpty() && !m_currentFilePath.isEmpty()) {
            QString applyError;
            if (task == "transcribe_subtitle")
                m_aiAssistantService.ApplyServerTranscript(segments, m_currentFilePath, &applyError);
            else if (task == "smart_edit")
                m_aiAssistantService.ApplyServerEditPlan(segments, m_currentFilePath, &applyError);
            if (!applyError.isEmpty())
                appendDebugMessage("服务端结果回注失败：" + applyError);
        }
        if (success)
            emit m_aiAssistantService.taskFinished(names.value(task, task), message.payload.value("result").toString());
        else
            emit m_aiAssistantService.taskFailed(names.value(task, task), message.payload.value("error").toString());
        return;
    }
    if (message.type == "plan") { m_agentPanel->SetPlan(message.payload.value("steps").toArray()); return; }
    if (message.type == "status") {
        if (message.sessionId != "desktop") return;
        const QString stage = message.payload.value("stage").toString();
        const QString tool = message.tool;
        const QString callId = message.payload.value("call_id").toString();
        if (stage == "tool_started") m_agentPanel->BeginToolExecution(tool, callId);
        else if (stage == "tool_completed")
            m_agentPanel->CompleteToolExecution(tool, callId, message.payload.value("success").toBool(),
                message.payload.value("success").toBool() ? QString{} : message.payload.value("status").toString());
        m_agentPanel->SetRuntimeStatus(message.payload.value("status").toString(), true);
        return;
    }
    if (message.type == "final") {
        m_agentPanel->SetAgentOutput(message.payload.value("text").toString());
        m_agentPanel->SetRuntimeStatus("任务已结束", true);
        return;
    }
    if (message.type == "error") {
        const QString error = message.payload.value("error").toString();
        m_agentPanel->SetAgentOutput(error);
        m_agentPanel->SetRuntimeStatus("任务失败：" + error, false);
        return;
    }
    if (message.type != "tool_call") return;
    const QString tool = message.tool;
    if (tool == "export_clip") { m_agentPendingTool = tool; m_agentPendingRequestId = message.requestId; m_agentPendingSessionId = message.sessionId; m_aiAssistantService.ExportEditPlan(message.arguments.value("output_dir").toString()); return; }
    const QJsonObject result = m_agentTools.Dispatch(tool, message.arguments);
    m_agentBridge.SendToolResult(message.requestId, message.sessionId, tool, result);
}

void MainWindow::onTick() {
    m_synchronizer.Tick();
    updateProgress();
}

void MainWindow::StartBenchmark(const QString& filePath, int seconds, bool sticker,
                                bool record, const QString& outputPath, const QString& capturePath) {
    m_playerWidget->SetStickerEnabled(sticker);
    startPlayback(filePath);
    if (record) {
        // Streaming playback connects asynchronously after upload; give the
        // source time to come up before toggling the server-side recorder.
        QTimer::singleShot(m_streamingMode ? 2500 : 0, this, [this] {
            if (m_hasOpenedFile) onRecordToggled(true);
        });
    }
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
    if (m_streamingMode && m_streamingSource && m_hasOpenedFile) {
        std::string error;
        if (!m_streamingSource->Control("seek", 0.0f, nullptr, &error)) {
            appendDebugMessage("AI 助手：跳转失败：" + QString::fromStdString(error));
            return false;
        }
        appendDebugMessage("AI 助手：已请求跳转到视频开头。");
        return true;
    }
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
    m_streamStatusTimer.stop();
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
        m_controllerWidget->SetScreenshotEnabled(false);
    }
}

void MainWindow::uploadMediaAsync(const QString& sessionId, const QString& filePath,
                                  std::function<void(const UploadOutcome&)> done) {
    auto* watcher = new QFutureWatcher<UploadOutcome>(this);
    connect(watcher, &QFutureWatcher<UploadOutcome>::finished, this,
            [watcher, done = std::move(done)]() {
                const UploadOutcome outcome = watcher->result();
                watcher->deleteLater();
                done(outcome);
            });
    const QString serverAddress = m_agentBridge.ServerAddress();
    watcher->setFuture(QtConcurrent::run([serverAddress, sessionId, filePath]() -> UploadOutcome {
        QString mediaId;
        QString error;
        const bool ok = av::agent::AgentBridge::UploadMediaToServer(
            serverAddress, filePath, sessionId, &mediaId, &error);
        return {ok, mediaId, error};
    }));
}

void MainWindow::startDesktopUpload(const QString& filePath, bool connectWhenReady) {
    if (m_desktopMediaId.isEmpty() || m_desktopMediaFilePath != filePath) {
        if (m_desktopUploadInProgress) return;
        m_desktopUploadInProgress = true;
        uploadMediaAsync(QStringLiteral("desktop"), filePath,
                         [this, filePath, connectWhenReady](const UploadOutcome& outcome) {
            m_desktopUploadInProgress = false;
            if (m_currentFilePath != filePath) {
                // The user switched media while this upload was running.
                if (!m_currentFilePath.isEmpty()) {
                    startDesktopUpload(m_currentFilePath, m_streamingMode);
                }
                return;
            }
            if (!outcome.ok) {
                appendDebugMessage("媒体上传失败（desktop）：" + outcome.error);
                if (!m_pendingGoal.isEmpty()) {
                    m_pendingGoal.clear();
                    m_agentPanel->SetRuntimeStatus("媒体上传失败，无法执行目标。", false);
                }
                return;
            }
            m_desktopMediaId = outcome.mediaId;
            m_desktopMediaFilePath = filePath;
            appendDebugMessage("媒体已上传到 Agent 服务（desktop）。");
            if (connectWhenReady) {
                connectStreamingPlayback(filePath, outcome.mediaId);
            }
            if (!m_pendingGoal.isEmpty()) {
                const QString goal = m_pendingGoal;
                m_pendingGoal.clear();
                m_agentPanel->ResetExecution();
                m_agentPanel->SetRuntimeStatus("正在提交任务…", true);
                m_agentBridge.SubmitGoal(goal, QStringLiteral("desktop"));
            }
        });
        return;
    }
    if (connectWhenReady) connectStreamingPlayback(filePath, m_desktopMediaId);
    if (!m_pendingGoal.isEmpty()) {
        const QString goal = m_pendingGoal;
        m_pendingGoal.clear();
        m_agentBridge.SubmitGoal(goal, QStringLiteral("desktop"));
    }
}

void MainWindow::connectStreamingPlayback(const QString& filePath, const QString& mediaId) {
    if (m_currentFilePath != filePath || mediaId.isEmpty()) return;
    m_streamingSource = std::make_unique<av::WebRtcPlaybackSource>(m_playerWidget);
    std::string connectError;
    if (!m_streamingSource->Connect(m_mediaServerAddress.toStdString(), "desktop",
                                    mediaId.toStdString(), 5000, &connectError)) {
        appendDebugMessage("连接媒体服务端失败：" + QString::fromStdString(connectError));
        m_streamingSource.reset();
        return;
    }
    std::cout << "[MainWindow] streaming playback connected media_id="
              << mediaId.toStdString() << std::endl;
    m_networkSession.Start(filePath.toStdString());
    refreshNetworkPanel();
    m_hasOpenedFile = true;
    m_isPlaying = true;
    if (m_controllerWidget) {
        m_controllerWidget->SetPlaying(true);
        m_controllerWidget->SetRecordEnabled(true);
        m_controllerWidget->SetScreenshotEnabled(true);
    }
    m_durationSeconds = 0.0f;
    if (m_progressSlider) m_progressSlider->setValue(0);
    m_streamStatusTimer.start();
    if (m_pendingStreamingRecord) {
        av::WebRtcPlaybackSource::PlaybackStatus status;
        std::string error;
        if (m_streamingSource->Control("record_start", 0.0f, &status, &error))
            m_controllerWidget->SetRecording(true);
        else
            appendDebugMessage("服务端录制启动失败：" + QString::fromStdString(error));
        m_pendingStreamingRecord = false;
    }
    syncAiContext();
}

void MainWindow::submitAiTask(const QString& task, const QJsonObject& arguments) {
    if (task.isEmpty()) return;
    if (!m_agentBridge.IsRunning() && !m_agentBridge.Start()) {
        emit m_aiAssistantService.taskFailed(task, "无法连接 Agent 服务。请先启动 services/agent。");
        return;
    }
    if (m_aiMediaId.isEmpty() || m_aiMediaFilePath != m_currentFilePath) {
        if (m_currentFilePath.isEmpty()) {
            emit m_aiAssistantService.taskFailed(task, "请先打开本地视频。");
            return;
        }
        m_pendingAiTask = task;
        m_pendingAiArguments = arguments;
        if (m_aiUploadInProgress) return;
        m_aiUploadInProgress = true;
        const QString filePath = m_currentFilePath;
        uploadMediaAsync(QStringLiteral("ai-sidebar"), filePath,
                         [this, filePath](const UploadOutcome& outcome) {
            m_aiUploadInProgress = false;
            if (m_currentFilePath != filePath) return;
            if (!outcome.ok) {
                const QString failedTask = m_pendingAiTask;
                m_pendingAiTask.clear();
                m_pendingAiArguments = {};
                emit m_aiAssistantService.taskFailed(failedTask, "媒体上传失败：" + outcome.error);
                return;
            }
            m_aiMediaId = outcome.mediaId;
            m_aiMediaFilePath = filePath;
            if (m_pendingAiTask.isEmpty()) return;
            const QString pendingTask = m_pendingAiTask;
            const QJsonObject pendingArguments = m_pendingAiArguments;
            m_pendingAiTask.clear();
            m_pendingAiArguments = {};
            m_agentBridge.SubmitTask(pendingTask, pendingArguments, QStringLiteral("ai-sidebar"));
        });
        return;
    }
    m_agentBridge.SubmitTask(task, arguments, QStringLiteral("ai-sidebar"));
}

void MainWindow::startPlayback(const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    m_currentFilePath = filePath;
    releaseFileReader();
    m_synchronizer.Stop();
    if (m_streamingSource) {
        m_streamingSource->Disconnect();
        m_streamingSource.reset();
    }
    m_desktopMediaId.clear();
    m_desktopMediaFilePath.clear();
    m_aiMediaId.clear();
    m_aiMediaFilePath.clear();
    m_pendingAiTask.clear();
    m_pendingAiArguments = {};
    m_pendingGoal.clear();
    m_pendingStreamingRecord = false;
    if (!m_streamingMode) createFileReader();

    // Uploads use their own gRPC channel, so playback does not need to start
    // (or wait for) the Agent Converse worker. Local playback starts
    // immediately; the single desktop-session upload runs in the background.
    if (m_streamingMode) {
        startDesktopUpload(filePath, /*connectWhenReady=*/true);
        return;
    }
    startDesktopUpload(filePath, /*connectWhenReady=*/false);

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
        m_controllerWidget->SetScreenshotEnabled(true);
    }
    syncAiContext();
}

void MainWindow::stopRecording(bool promptForSavePath) {
    if (m_controllerWidget) {
        m_controllerWidget->SetRecording(false);
    }
    if (m_streamingMode) {
        if (m_streamingSource) {
            av::WebRtcPlaybackSource::PlaybackStatus status;
            std::string error;
            if (m_streamingSource->Control("record_stop", 0.0f, &status, &error)) {
                if (promptForSavePath && !status.outputPath.empty()) {
                    appendDebugMessage("录制已保存：" + QString::fromStdString(status.outputPath));
                    QMessageBox::information(this, "录制已保存",
                                             QString::fromStdString(status.outputPath));
                }
            } else {
                appendDebugMessage("停止服务端录制失败：" + QString::fromStdString(error));
            }
        }
        syncAiContext();
        return;
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
    const bool loaded = m_hasOpenedFile && !m_currentFilePath.isEmpty();
    const bool recording = m_recordSession && m_recordSession->IsRecording();
    const int width = m_fileReader ? m_fileReader->GetVideoWidth() : 0;
    const int height = m_fileReader ? m_fileReader->GetVideoHeight() : 0;
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

void MainWindow::updateStreamingStatus() {
    if (!m_streamingMode || !m_streamingSource) {
        m_streamStatusTimer.stop();
        return;
    }
    av::WebRtcPlaybackSource::PlaybackStatus status;
    std::string error;
    if (!m_streamingSource->Control("status", 0.0f, &status, &error) || !status.success) {
        return;
    }
    m_durationSeconds = static_cast<float>(status.durationMs / 1000.0);
    if (m_progressSlider && !m_progressSlider->isSliderDown() && m_durationSeconds > 0.0f) {
        const double progress = std::clamp(status.positionMs / status.durationMs, 0.0, 1.0);
        m_progressSlider->setValue(static_cast<int>(progress * m_progressSlider->maximum()));
    }
    m_isPlaying = status.playing;
    if (m_controllerWidget) {
        m_controllerWidget->SetPlaying(status.playing);
        m_controllerWidget->SetRecording(status.recording);
    }
}

void MainWindow::updateProgress() {
    if (m_streamingMode) {
        return;
    }
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
