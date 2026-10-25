//
// Created by Joe on 26-3-15.
//

#include "PlayerWidget.h"

#include <QMetaObject>
#include <algorithm>
#include <cmath>
#include <iostream>

PlayerWidget::PlayerWidget(QWidget *parent) : QOpenGLWidget(parent) {
    setMinimumSize(960, 540);
    connect(this, &QOpenGLWidget::frameSwapped, this, &PlayerWidget::OnFrameSwapped);
}

PlayerWidget::~PlayerWidget() {
    ReportMetrics();
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
    ReportMetrics();
    m_videoDisplay.ClearFrame();
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

void PlayerWidget::SetMetricsSources(std::function<double()> audioClock, std::function<bool()> recording) {
    m_audioClock = std::move(audioClock);
    m_recording = std::move(recording);
}

void PlayerWidget::FlushMetrics() { ReportMetrics(); }

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
    std::cout << "[PlaybackMetrics] OpenGL vendor=" << glGetString(GL_VENDOR)
              << " renderer=" << glGetString(GL_RENDERER) << std::endl;
    m_videoDisplay.Initialize(this);
}

void PlayerWidget::resizeGL(int w, int h) {
    m_videoDisplay.Resize(this, w, h);
}

void PlayerWidget::paintGL() {
    m_frameForSwap = m_videoDisplay.Render(this);
}

void PlayerWidget::OnFrameSwapped() {
    if (!m_frameForSwap) return;
    const auto now = std::chrono::steady_clock::now();
    if (m_swappedFrames == 0) m_metricsStart = now;
    ++m_swappedFrames;
    if (m_audioClock) {
        const double audioSeconds = m_audioClock();
        if (audioSeconds >= 0.0) {
            const double signedErrorMs = (m_frameForSwap->GetTimeStamp() - audioSeconds) * 1000.0;
            m_signedSyncSumMs += signedErrorMs;
            m_absSyncErrorsMs.push_back(std::abs(signedErrorMs));
        }
    }
    m_frameForSwap.reset();
    if (std::chrono::duration<double>(now - m_metricsStart).count() >= 10.0) ReportMetrics();
}

void PlayerWidget::ReportMetrics() {
    if (m_swappedFrames < 2) return;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_metricsStart).count();
    const auto [faceAttempts, faceHits, landmarkHits] = m_videoDisplay.TakeFaceDetectionCounts();
    std::cout << "[PlaybackMetrics] swapped_fps=" << (m_swappedFrames - 1) / seconds
              << " frames=" << m_swappedFrames << " seconds=" << seconds
              << " resolution=" << m_videoDisplay.FrameWidth() << "x" << m_videoDisplay.FrameHeight()
              << " sticker=" << (m_videoDisplay.StickerEnabled() ? 1 : 0)
              << " recording=" << (m_recording && m_recording() ? 1 : 0)
              << " face_detection_attempts=" << faceAttempts
              << " face_detection_hits=" << faceHits
              << " landmark_hits=" << landmarkHits;
    if (!m_absSyncErrorsMs.empty()) {
        std::sort(m_absSyncErrorsMs.begin(), m_absSyncErrorsMs.end());
        const size_t p95 = static_cast<size_t>(0.95 * (m_absSyncErrorsMs.size() - 1));
        std::cout << " av_delta_definition=video_pts_minus_audio_clock"
                  << " av_samples=" << m_absSyncErrorsMs.size()
                  << " av_signed_mean_ms=" << m_signedSyncSumMs / m_absSyncErrorsMs.size()
                  << " av_abs_p95_ms=" << m_absSyncErrorsMs[p95]
                  << " av_abs_max_ms=" << m_absSyncErrorsMs.back();
    }
    std::cout << std::endl;
    m_swappedFrames = 0;
    m_absSyncErrorsMs.clear();
    m_signedSyncSumMs = 0.0;
}
