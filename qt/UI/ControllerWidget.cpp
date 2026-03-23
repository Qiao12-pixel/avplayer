//
// Created by Joe on 26-3-15.
//

#include "ControllerWidget.h"

#include <QFileDialog>


ControllerWidget::ControllerWidget(QWidget *parent) : QWidget(parent){
    //初始化按钮
    m_btnImport = new QPushButton("导入", this);
    connect(m_btnImport, &QPushButton::clicked, this, &ControllerWidget::onImportBtnClicked);
    m_btnPlay = new QPushButton("播放", this);
    m_btnExport = new QPushButton("录制", this);
    m_btnVideoFilterFlipVertical = new QPushButton("添加垂直翻转滤镜", this);
    m_btnVideoFilterGray = new QPushButton("添加黑白颜色滤镜", this);
    m_btnVideoFilterInvert = new QPushButton("添加颜色反转滤镜", this);
    m_btnVideoFilterSticker = new QPushButton("添加贴纸", this);

    //布局
    auto* layout = new QHBoxLayout(this);
    layout->addWidget(m_btnImport);
    layout->addWidget(m_btnPlay);
    layout->addWidget(m_btnExport);
    layout->addStretch();
    layout->addWidget(m_btnVideoFilterFlipVertical);
    layout->addWidget(m_btnVideoFilterGray);
    layout->addWidget(m_btnVideoFilterInvert);
    layout->addWidget(m_btnVideoFilterSticker);
    setLayout(layout);
}
ControllerWidget::~ControllerWidget() {

}

void ControllerWidget::onImportBtnClicked() {
    QString videoFilePath = QFileDialog::getOpenFileName(
        this, "打开视频文件", QDir::currentPath(), "视频文件(*.mp4;*.avi);;所有文件(*)");
    if (videoFilePath.isEmpty()) {
        QMessageBox::warning(this, "错误", "文件未能打开");
    }
    qDebug() << "文件路径:" << videoFilePath;
}



