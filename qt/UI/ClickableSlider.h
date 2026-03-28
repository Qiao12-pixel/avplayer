//
// Created by Codex on 26-3-27.
//

#ifndef CLICKABLESLIDER_H
#define CLICKABLESLIDER_H

#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

class ClickableSlider : public QSlider {
public:
    explicit ClickableSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent) {}

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            QStyleOptionSlider option;
            initStyleOption(&option);
            const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
            const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);

            int value = minimum();
            if (orientation() == Qt::Horizontal) {
                const int sliderMin = groove.x();
                const int sliderMax = groove.right() - handle.width() + 1;
                value = QStyle::sliderValueFromPosition(minimum(), maximum(),
                                                        event->position().x() - handle.width() / 2,
                                                        std::max(1, sliderMax - sliderMin));
            } else {
                const int sliderMin = groove.y();
                const int sliderMax = groove.bottom() - handle.height() + 1;
                value = QStyle::sliderValueFromPosition(minimum(), maximum(),
                                                        event->position().y() - handle.height() / 2,
                                                        std::max(1, sliderMax - sliderMin), true);
            }

            setValue(value);
            emit sliderMoved(value);
            emit sliderPressed();
            emit sliderReleased();
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }
};

#endif //CLICKABLESLIDER_H
