//
// Created by Joe on 26-3-3.
//
// #include <QApplication>
// #include <QWidget>
// #include <QSurfaceFormat>
// #include <QTranslator>
//
// #include "MainWindow.h"
// int main(int argc, char *argv[]) {
//     //设置共享OpenGL上下文属性[多个窗口共用同一份缓冲区]
//     QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
//     //配置OpenGL Surface格式
//     QSurfaceFormat fmt;
//     fmt.setDepthBufferSize(24);
//     fmt.setVersion(3, 3);
//     fmt.setProfile(QSurfaceFormat::CoreProfile);
//     QSurfaceFormat::setDefaultFormat(fmt);
//     QApplication app(argc, argv);
//     qDebug() << "音视频播放器执行中...";
//     // MainWindow w;
//     // w.show();
//     return app.exec();
// }


// int main(int argc, char *argv[]) {
//     std::cout << "音视频播放器..." << std::endl;
//     std::cout << "=============解码器测试解复用开始=========" << std::endl;
//     av::IDeMuxer* demuxer = av::IDeMuxer::Create();
//     av::MiniDecoder* decoder = new av::MiniDecoder();
//
//     //绑定监听器
//     demuxer->SetListener(decoder);
//
//     std::string filePath = "/Users/joe/CLionProjects/AVPlayer/resource/input.mp4";
//     if (!demuxer->Open(filePath)) {
//         std::cerr << "打开文件失败" << filePath << std::endl;
//         delete demuxer;
//         delete decoder;
//         demuxer = nullptr;
//         decoder = nullptr;
//         return -1;
//     }
//     std::cout << "视频总时长: " << demuxer->GetDuration() << " 秒" << std::endl;
//     //开始解复用
//     demuxer->Start();
//     std::this_thread::sleep_for(std::chrono::seconds(2));
//     //测试seek
//     demuxer->SeekTo(0.5f);
//     std::this_thread::sleep_for(std::chrono::seconds(2));
//     demuxer->Stop();
//
//     delete demuxer;
//     delete decoder;
//     demuxer = nullptr;
//     decoder = nullptr;
//
//     std::cout << "=============解码器测试解复用结束=========" << std::endl;
//     return 0;
// }
#include <iostream>

#include "../AV/src/Reader/FileReader.h"
#include <thread>
namespace av {
    class FrameTester : public av::IFileReader::Listener {
    public:
        //继承IFileReader::Listener
        void OnFileReaderNotifyVideoFrame(std::shared_ptr<IVideoFrame> frame) override {
            // 1. 拦截 Flush 刷新帧
            if (frame->flags & AVFrameFlag::KFlush) {
                std::cout << "[Video] 收到 刷新帧 (Flush Frame)，无像素数据，跳过打印。" << std::endl;
                return;
            }

            // 2. 安全检查：确保画面数据指针不为空
            if (!frame->data) {
                std::cout << "[Video] 警告：收到一个没有像素数据的异常帧！" << std::endl;
                return;
            }
            static int videoCount = 0;
            videoCount++;
            std::cout << "[Video] 收到第 " << videoCount << " 帧! "
                  << "分辨率: " << frame->width << "x" << frame->height
                  << ", PTS: " << frame->pts
                  << ", 第一格像素值: " << (int)frame->data.get()[0] // 打印第一个 RGBA 字节，证明数据真的存在
                  << std::endl;
        }
        void OnFileReaderNotifyAudioSamples(std::shared_ptr<IAudioSamples> samples) override {
            static int audioCount = 0;
            audioCount++;
            std::cout << "[Audio] 收到第 " << audioCount << " 个音频包! "
                  << "采样率: " << samples->sampleRate
                  << " PTS: " << samples->pts << std::endl;
        }
        void OnFileReaderNotifyAudioFinished() override {
            std::cout << "--- 音频流结束 ---" << std::endl;
        }
        void OnFileReaderNotifyVideoFinished() override {
            std::cout << "--- 视频流结束 ---" << std::endl;
        }
    };
}

int main() {
    std::cout << "======验证是否能拿到frame=========" << std::endl;
    av::FileReader m_fileReader;
    av::FrameTester m_frameTester;
    //m_frameTester是消费者， m_fileReader是生产者
    m_fileReader.SetListener(&m_frameTester);
    std::string videoPath = "/Users/joe/CLionProjects/AVPlayer/resource/input.mp4";

    if (!m_fileReader.Open(videoPath)) {
        std::cerr << "failed to open video path" << std::endl;
        return -1;
    }
    std::cout << "enable to open video Path. start to fileReader..." << std::endl;

    m_fileReader.Start();//启动解码器，音频解码器，视频解码器

    //这里是主线程，等待后台进行输出
    for (int i = 0; i < 100; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    //测试结束
    std::cout << "fileReader stop..." << std::endl;
    m_fileReader.Stop();

}