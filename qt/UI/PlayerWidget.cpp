//
// Created by Joe on 26-3-15.
//

#include "PlayerWidget.h"

#include <QMetaObject>

PlayerWidget::PlayerWidget(QWidget *parent) : QOpenGLWidget(parent) {
    setMinimumSize(960, 540);
}

PlayerWidget::~PlayerWidget() {
    if (context()) {
        makeCurrent();
        m_videoDisplay.Cleanup(this);
        doneCurrent();
    }
}

void PlayerWidget::PushFrame(const std::shared_ptr<av::IVideoFrame>& frame) {
    m_videoDisplay.PushFrame(frame);
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

void PlayerWidget::ClearFrame() {
    m_videoDisplay.ClearFrame();
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

void PlayerWidget::SetFilterEnabled(av::VideoFilterType type, bool enabled) {
    m_videoDisplay.SetFilterEnabled(type, enabled);
    update();
}

void PlayerWidget::SetFlipEnabled(bool enabled) {
    SetFilterEnabled(av::VideoFilterType::kFlipVertical, enabled);
}

void PlayerWidget::SetGrayEnabled(bool enabled) {
    SetFilterEnabled(av::VideoFilterType::kGray, enabled);
}

void PlayerWidget::SetInvertEnabled(bool enabled) {
    SetFilterEnabled(av::VideoFilterType::kInvert, enabled);
}

void PlayerWidget::SetStickerEnabled(bool enabled) {
    SetFilterEnabled(av::VideoFilterType::kSticker, enabled);
}

void PlayerWidget::CycleStickerResource() {
    m_videoDisplay.CycleStickerResource();
    update();
}

void PlayerWidget::initializeGL() {
    initializeOpenGLFunctions();
    m_videoDisplay.Initialize(this);
}

void PlayerWidget::resizeGL(int w, int h) {
    m_videoDisplay.Resize(this, w, h);
}

void PlayerWidget::paintGL() {
    m_videoDisplay.Render(this);
}
