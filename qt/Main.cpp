//
// Created by Joe on 26-3-3.
//
#include <QApplication>
#include <QWidget>
#include <QSurfaceFormat>
#include <QTranslator>

#include "MainWindow.h"
int main(int argc, char *argv[]) {
    //设置共享OpenGL上下文属性[多个窗口共用同一份缓冲区]
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    //配置OpenGL Surface格式
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}