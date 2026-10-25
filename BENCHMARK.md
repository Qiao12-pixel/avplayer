# 播放性能与同步测量

当前播放链路在 `VideoDecoder` 中保留紧凑 YUV420P：输入本身是 YUV420P 时只复制平面；其他格式先由 FFmpeg `sws_scale` 在 CPU 上转为 YUV420P。`VideoDisplay` 将 Y、U、V 上传为三张单通道纹理，在片段着色器中完成 YUV→RGB 和滤镜合成。OpenCV 贴纸检测需要像素数据时单独在 CPU 上转为 RGBA；录制编码器直接使用 YUV420P。视频解码仍使用 FFmpeg 常规解码器，不能称为硬件解码。

构建时需选用包含 `face` 模块的 OpenCV，并使 QtCore、QtMultimedia 等 Qt 模块版本一致。本机使用 `OpenCV_DIR=/opt/homebrew/lib/cmake/opencv5` 与 Qt 6.10.2；生成的可执行文件位于构建目录中。

CLion 重新加载 CMake 后可直接构建 `AVPlayer`。由于本机 Homebrew 的 `qtbase` 6.11.1 与 `qtmultimedia` 6.10.2 并存，运行时请使用项目根目录的 `run_avplayer.sh`，它会把 Qt 框架和平台插件固定到 6.10.2：

```bash
./run_avplayer.sh cmake-build-debug --benchmark resource/input.mp4 --seconds 5
```

播放器提供可重复运行的基准入口：

```bash
ffmpeg -f lavfi -i 'testsrc2=size=1920x1080:rate=60' \
  -f lavfi -i 'sine=frequency=1000:sample_rate=44100' \
  -t 15 -c:v libx264 -preset ultrafast -crf 24 -pix_fmt yuv420p \
  -c:a aac -b:a 128k -y /tmp/avplayer-1080p60-h264.mp4

./AVPlayer --benchmark /tmp/avplayer-1080p60-h264.mp4 --seconds 12 \
  --sticker --record /tmp/avplayer-recorded.mp4
```

`--sticker` 和 `--record OUTPUT.mp4` 可分别省略，以比较不同负载。还可使用 `--capture FRAME.png` 保存约第 2 秒的 OpenGL 帧，检查颜色和纹理方向。运行日志以 `[PlaybackMetrics]` 开头，记录 OpenGL 渲染器、输入编码、分辨率、源帧率及每个约 10 秒窗口的结果。`swapped_fps` 只计入有新视频帧的 `QOpenGLWidget::frameSwapped` 事件；`face_detection_hits` 表示人脸框检测成功，`landmark_hits` 才表示通过有效性检查的 68 点结果。同步差值定义为 **视频帧 PTS − `QAudioSink` 推算的音频时钟**，在帧交换信号处采样，输出有符号均值、绝对值 P95 和最大值。这里的音频时钟来自 `processedUSecs()`，并未测量扬声器实际出声时刻，不能把该统计值称作物理音画延迟。

2026-10-11，在 Apple M5、macOS 26.6.1、Qt 6.10.2、Debug 构建下，将仓库自带的 1280×720/24 fps H.264 样片临时缩放并补帧为 1920×1080/60 fps H.264，开启贴纸和 H.264/AAC 录制后测得：

| 窗口 | `swapped_fps` | 同步差值绝对值 P95 | 人脸框命中 | 有效 68 点 |
| --- | ---: | ---: | ---: | ---: |
| 前约 10 秒 | 60.30 | 35.55 ms | 66 / 100 次检测 | 0 |
| 后约 2 秒 | 58.37 | 42.74 ms | 15 / 20 次检测 | 0 |

这段输入由 24 fps 样片补帧，不能代表原生 60 fps 摄制内容。有效 68 点为 0：本机 OpenCV 5.0.0 的 LBF 对此模型返回全零点；代码会拒绝这些点，改用人脸框估算贴纸位置。**当前不能声称 LBF 68 关键点跟踪已经通过实测。**导出的 MP4 已由 `ffprobe` 确认为 1920×1080 H.264 视频和 AAC 音频。上述数据支持“该条件下约 60 fps”的表述；“稳定 60 fps”和物理音画同步精度需要代表性原生素材、Release 构建及更长时间的重复测试。
