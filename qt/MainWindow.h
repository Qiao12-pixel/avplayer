//
// Created by Joe on 26-3-3.
//

#ifndef MAINWINDOW_H
#define MAINWINDOW_H


#include <QMainWindow>
#include <QMouseEvent>
#include <QLabel>
#include <QResizeEvent>
#include <QSlider>
#include <QPushButton>
#include <iostream>
#include <QFile>
#include <QFileDialog>
#include "./UI/PlayerWidget.h"
#include "UI/ControllerWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSliderMoved(int value);

private:
    //核心显示组件
    //QWidget* m_playerWidget = nullptr;
    QSlider* m_progressSlider = nullptr;
    PlayerWidget* m_playerWidget= nullptr;
    // //底部按钮组件
    // QPushButton* m_btnPlay = nullptr;
    // QPushButton* m_btnRecord = nullptr;
    // QPushButton* m_btnStop = nullptr;
    // //滤镜组件
    // QPushButton* m_btnFilterFlip = nullptr;
    // QPushButton* m_btnFilterGray = nullptr;
    // QPushButton* m_btnFilterInvert = nullptr;
    // QPushButton* m_btnFilterSticker = nullptr;
    ControllerWidget* m_controllerWidget{nullptr};
};



#endif //MAINWINDOW_H
