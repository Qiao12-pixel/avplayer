//
// Created by Joe on 26-3-15.
//

#include "ControllerWidget.h"

#include <QFileDialog>

ControllerWidget::ControllerWidget(QWidget *parent) : QWidget(parent){
    m_btnImport = new QPushButton("导入", this);
    connect(m_btnImport, &QPushButton::clicked, this, &ControllerWidget::onImportBtnClicked);

    m_btnPlay = new QPushButton("播放", this);
    connect(m_btnPlay, &QPushButton::clicked, this, &ControllerWidget::onPlayBtnClicked);

    m_btnExport = new QPushButton("录制", this);
    m_btnExport->setEnabled(false);
    connect(m_btnExport, &QPushButton::clicked, this, &ControllerWidget::onRecordBtnClicked);

    m_btnVideoFilterFlipVertical = new QPushButton("垂直翻转", this);
    m_btnVideoFilterGray = new QPushButton("黑白滤镜", this);
    m_btnVideoFilterInvert = new QPushButton("反相滤镜", this);
    m_btnVideoFilterSticker = new QPushButton("贴纸滤镜", this);
    m_btnStickerAssetSwitch = new QPushButton("切换贴纸", this);

    m_btnVideoFilterFlipVertical->setCheckable(true);
    m_btnVideoFilterGray->setCheckable(true);
    m_btnVideoFilterInvert->setCheckable(true);
    m_btnVideoFilterSticker->setCheckable(true);

    connect(m_btnVideoFilterFlipVertical, &QPushButton::toggled, this, &ControllerWidget::flipFilterToggled);
    connect(m_btnVideoFilterGray, &QPushButton::toggled, this, &ControllerWidget::grayFilterToggled);
    connect(m_btnVideoFilterInvert, &QPushButton::toggled, this, &ControllerWidget::invertFilterToggled);
    connect(m_btnVideoFilterSticker, &QPushButton::toggled, this, &ControllerWidget::stickerFilterToggled);
    connect(m_btnStickerAssetSwitch, &QPushButton::clicked, this, &ControllerWidget::stickerAssetSwitchRequested);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(m_btnImport);
    layout->addWidget(m_btnPlay);
    layout->addWidget(m_btnExport);
    layout->addStretch();
    layout->addWidget(m_btnVideoFilterFlipVertical);
    layout->addWidget(m_btnVideoFilterGray);
    layout->addWidget(m_btnVideoFilterInvert);
    layout->addWidget(m_btnVideoFilterSticker);
    layout->addWidget(m_btnStickerAssetSwitch);
    setLayout(layout);
}

ControllerWidget::~ControllerWidget() = default;

void ControllerWidget::SetPlaying(bool playing) {
    m_btnPlay->setText(playing ? "暂停" : "播放");
}

void ControllerWidget::SetRecording(bool recording) {
    m_btnExport->setText(recording ? "停止录制" : "录制");
}

void ControllerWidget::SetRecordEnabled(bool enabled) {
    m_btnExport->setEnabled(enabled);
}

void ControllerWidget::onImportBtnClicked() {
    const QString videoFilePath = QFileDialog::getOpenFileName(
        this, "打开视频文件", QDir::currentPath(), "视频文件(*.mp4;*.avi;*.mov;*.mkv);;所有文件(*)");
    if (videoFilePath.isEmpty()) {
        return;
    }
    emit importRequested(videoFilePath);
}

void ControllerWidget::onPlayBtnClicked() {
    const bool nextStateIsPlaying = m_btnPlay->text() == "播放";
    SetPlaying(nextStateIsPlaying);
    emit playToggled(nextStateIsPlaying);
}

void ControllerWidget::onRecordBtnClicked() {
    const bool nextStateIsRecording = m_btnExport->text() == "录制";
    emit recordToggled(nextStateIsRecording);
}
