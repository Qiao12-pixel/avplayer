//
// Created by Joe on 26-3-3.
//

#include "MainWindow.h"

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>

#include "../AV/src/Reader/FileReader.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_synchronizer.Init();

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

    m_sidePanel = new QWidget(this);
    m_sidePanel->setMinimumWidth(360);
    m_sidePanel->setStyleSheet("background: rgba(7, 12, 22, 0.78); border: 1px solid rgba(148, 163, 184, 0.20); border-radius: 18px;");
    auto* sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(16, 16, 16, 16);
    sideLayout->setSpacing(10);

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

    sideLayout->addWidget(panelTitle);
    sideLayout->addSpacing(4);
    sideLayout->addWidget(sourceTitle);
    sideLayout->addWidget(m_currentSourceValueLabel);
    sideLayout->addSpacing(4);
    sideLayout->addWidget(networkStateTitle);
    sideLayout->addWidget(m_networkStatusValueLabel);
    sideLayout->addSpacing(4);
    sideLayout->addWidget(errorTitle);
    sideLayout->addWidget(m_errorValueLabel);
    sideLayout->addSpacing(6);
    sideLayout->addWidget(networkTitle);
    sideLayout->addWidget(m_networkUrlEdit);
    sideLayout->addLayout(buttonRow);
    sideLayout->addWidget(debugTitle);
    sideLayout->addWidget(m_debugLabel, 1);

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
    appendDebugMessage("Qt 网络框架已初始化，等待输入地址或导入本地文件。");
}

MainWindow::~MainWindow() {
    releaseFileReader();
    m_synchronizer.Stop();
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
    m_synchronizer.PushAudioSamples(audioSamples);
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
    m_synchronizer.PushVideoFrame(videoFrame);
}

void MainWindow::OnFileReaderNotifyAudioFinished() {
    std::cout << "[MainWindow] audio stream finished" << std::endl;
}

void MainWindow::OnFileReaderNotifyVideoFinished() {
    std::cout << "[MainWindow] video stream finished" << std::endl;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    releaseFileReader();
    m_synchronizer.Stop();
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
        return;
    }

    if (m_fileReader) {
        m_fileReader->Paused();
    }
    m_isPlaying = false;
    m_networkSession.Stop();
    refreshNetworkPanel();
}

void MainWindow::onTick() {
    m_synchronizer.Tick();
    updateProgress();
}

void MainWindow::createFileReader() {
    if (m_fileReader) {
        return;
    }
    m_fileReader = new av::FileReader();
    m_fileReader->SetListener(this);
}

void MainWindow::releaseFileReader() {
    if (!m_fileReader) {
        return;
    }
    m_fileReader->Stop();
    delete m_fileReader;
    m_fileReader = nullptr;
    m_hasOpenedFile = false;
    m_isPlaying = false;
    m_durationSeconds = 0.0f;
    if (m_progressSlider) {
        m_progressSlider->setValue(0);
    }
    refreshNetworkPanel();
    if (m_controllerWidget) {
        m_controllerWidget->SetPlaying(false);
    }
}

void MainWindow::startPlayback(const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }

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
    }
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
