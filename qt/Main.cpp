//
// Created by Joe on 26-3-3.
//

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSurfaceFormat>
#include <QTimer>
#include <iostream>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    const QString qtPluginPath = QStringLiteral("/opt/homebrew/Cellar/qtbase/6.10.2/share/qt/plugins");
    if (QFileInfo::exists(qtPluginPath)) {
        QCoreApplication::setLibraryPaths({qtPluginPath});
    }

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    const QStringList args = app.arguments();
    if (args.size() > 1 && args[1] == "--benchmark") {
        if (args.size() < 3) {
            std::cerr << "usage: AVPlayer --benchmark FILE [--seconds N] [--sticker] [--record OUTPUT.mp4] [--capture FRAME.png]" << std::endl;
            return 2;
        }
        int seconds = 20;
        bool sticker = false;
        bool record = false;
        QString outputPath;
        QString capturePath;
        for (int i = 3; i < args.size(); ++i) {
            if (args[i] == "--seconds" && i + 1 < args.size()) {
                seconds = args[++i].toInt();
            } else if (args[i] == "--sticker") {
                sticker = true;
            } else if (args[i] == "--record" && i + 1 < args.size()) {
                record = true;
                outputPath = args[++i];
            } else if (args[i] == "--capture" && i + 1 < args.size()) {
                capturePath = args[++i];
            } else {
                std::cerr << "unknown benchmark option: " << args[i].toStdString() << std::endl;
                return 2;
            }
        }
        const QString filePath = args[2];
        QTimer::singleShot(0, &window, [&window, filePath, seconds, sticker, record, outputPath, capturePath] {
            window.StartBenchmark(filePath, seconds, sticker, record, outputPath, capturePath);
        });
    }
    return app.exec();
}
