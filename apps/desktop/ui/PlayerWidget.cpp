//
// Created by Joe on 26-3-15.
//

#include "PlayerWidget.h"

#include <QFontMetrics>
#include <QMetaObject>
#include <QPainter>
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
    m_presentedMs = -1;
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

void PlayerWidget::SetSubtitleSegments(const QVector<av::ai::TranscriptSegment>& segments) {
    m_subtitleSegments = segments;
    std::sort(m_subtitleSegments.begin(), m_subtitleSegments.end(), [](const auto& a, const auto& b) {
        return a.startMs < b.startMs;
    });
    update();
}

void PlayerWidget::SetSubtitlesEnabled(bool enabled) {
    m_subtitlesEnabled = enabled;
    update();
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
    QPainter painter(this);
    painter.beginNativePainting();
    m_frameForSwap = m_videoDisplay.Render(this);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    painter.endNativePainting();
    if (m_frameForSwap) m_presentedMs = qRound64(m_frameForSwap->GetTimeStamp() * 1000.0);
    if (!m_subtitlesEnabled || m_presentedMs < 0 || m_subtitleSegments.isEmpty()) return;

    const auto it = std::find_if(m_subtitleSegments.cbegin(), m_subtitleSegments.cend(), [this](const auto& segment) {
        return segment.startMs <= m_presentedMs && m_presentedMs < segment.endMs;
    });
    if (it == m_subtitleSegments.cend() || it->text.trimmed().isEmpty()) return;

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont subtitleFont = font();
    subtitleFont.setPixelSize(20);
    subtitleFont.setWeight(QFont::DemiBold);
    painter.setFont(subtitleFont);

    const int maxTextWidth = std::max(100, width() * 3 / 4);
    const QFontMetrics metrics(subtitleFont);
    const QRect measured = metrics.boundingRect(QRect(0, 0, maxTextWidth, height() / 3),
        Qt::AlignCenter | Qt::TextWordWrap, it->text);
    const int boxWidth = std::min(width() - 24, std::max(100, measured.width() + 32));
    const int boxHeight = measured.height() + 20;
    const QRect box((width() - boxWidth) / 2,
        height() - std::max(44, height() / 8) - boxHeight, boxWidth, boxHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 178));
    painter.drawRoundedRect(box, 6, 6);
    painter.setPen(Qt::white);
    painter.drawText(box.adjusted(16, 10, -16, -10), Qt::AlignCenter | Qt::TextWordWrap, it->text);
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
