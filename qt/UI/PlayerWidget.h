//
// Created by Joe on 26-3-15.
//

#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QOpenGLWidget>

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

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    av::VideoDisplay m_videoDisplay;
};

#endif //PLAYERWIDGET_H
