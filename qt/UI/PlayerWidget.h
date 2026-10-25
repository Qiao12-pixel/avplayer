//
// Created by Joe on 26-3-15.
//

#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QOpenGLWidget>
#include <chrono>
#include <functional>
#include <vector>

#include "../../AV/include/IVideoFilter.h"
#include "../../AV/src/Display/VideoDisplay.h"

class PlayerWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PlayerWidget(QWidget* parent = nullptr);
    ~PlayerWidget() override;

    void PushFrame(const std::shared_ptr<av::IVideoFrame>& frame);
    void ClearFrame();
    void SetFilterEnabled(av::VideoFilterType type, bool enabled);
    void SetFlipEnabled(bool enabled);
    void SetGrayEnabled(bool enabled);
    void SetInvertEnabled(bool enabled);
    void SetStickerEnabled(bool enabled);
    void CycleStickerResource();
    void SetMetricsSources(std::function<double()> audioClock, std::function<bool()> recording);
    void FlushMetrics();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void OnFrameSwapped();
    void ReportMetrics();
    av::VideoDisplay m_videoDisplay;
    std::function<double()> m_audioClock;
    std::function<bool()> m_recording;
    std::shared_ptr<av::IVideoFrame> m_frameForSwap;
    std::chrono::steady_clock::time_point m_metricsStart{};
    int m_swappedFrames{0};
    std::vector<double> m_absSyncErrorsMs;
    double m_signedSyncSumMs{0.0};
};

#endif //PLAYERWIDGET_H
