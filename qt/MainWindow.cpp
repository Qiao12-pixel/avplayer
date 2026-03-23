//
// Created by Joe on 26-3-3.
//

#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    //创建中心布局
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    //设置垂直布局
    auto* vbox = new QVBoxLayout(centralWidget);
    vbox->setContentsMargins(0,0,0,0);
    vbox->setSpacing(5);

    //添加视频渲染区域
    m_playerWidget = new PlayerWidget(this);
    vbox->addWidget(m_playerWidget, 9);
    m_playerWidget->setAutoFillBackground(true);
    QPalette pal = m_playerWidget->palette();
    pal.setColor(QPalette::Window, Qt::black);
    m_playerWidget->setPalette(pal);

    //添加进度条
    m_progressSlider = new QSlider(Qt::Horizontal, this);
    m_progressSlider->setRange(0, 1000);
    vbox->addWidget(m_progressSlider);

    //添加按钮控件
    m_controllerWidget = new ControllerWidget(this);
    vbox->addWidget(m_controllerWidget);

    setMinimumSize(1280, 720);

}
MainWindow::~MainWindow(){}
void MainWindow::resizeEvent(QResizeEvent * event) {}
void MainWindow::mousePressEvent(QMouseEvent *event) {}
void MainWindow::mouseMoveEvent(QMouseEvent *event) {}
void MainWindow::mouseReleaseEvent(QMouseEvent *event) {}
void MainWindow::mouseDoubleClickEvent(QMouseEvent *event) {}
void MainWindow::onSliderMoved(int value) {}
