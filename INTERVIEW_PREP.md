# AVPlayer 面试复盘

面试日期：2026-05-22

## 1. 一句话介绍

AVPlayer 是一个基于 C++17、Qt6、FFmpeg、OpenGL 和 OpenCV 的音视频播放器项目。它支持本地文件和常见网络 URL 播放，包含解封装、音视频解码、音视频同步、OpenGL 实时滤镜、人脸贴纸、进度条 seek，以及 MP4 录制链路。

更适合后端面试的说法：

> 这个项目本质上是一个高并发的流式数据处理管线：输入端不断读取 packet，中间有多个异步解码和转换阶段，输出端要做音视频同步、渲染和录制。里面我重点处理了模块解耦、线程协作、背压控制、状态切换、时间戳对齐和资源生命周期。

## 2. 30 秒版本

这个项目我负责搭了一个 C++ 音视频播放和录制框架。核心链路是 `DeMuxer -> AudioDecoder/VideoDecoder -> AVSynchronizer -> AudioRender/VideoDisplay`。FFmpeg 负责解封装和编解码，Qt 做窗口和音频输出，OpenGL 做视频纹理渲染和 shader 滤镜，OpenCV 做人脸关键点贴纸。为了避免生产端过快导致内存膨胀，我做了基于 release callback 的背压；为了支持拖动进度条，我在 seek 后向音频、视频解码器发 flush 包并清空旧帧；录制部分用 H.264/AAC 编码后交给 MP4 muxer 写文件。

## 3. 3 分钟版本

项目分成 UI 层和 AV 内核层。UI 层主要是 `MainWindow`、`PlayerWidget` 和控制面板，负责接收用户导入文件、播放暂停、seek、滤镜开关、录制开关等操作。真正的播放链路在 AV 内核层。

打开文件或 URL 后，`FileReader` 创建并串联解封装器和音视频解码器。`DeMuxer` 使用 FFmpeg 打开输入源，识别音频流和视频流，然后在独立线程里持续读取 packet。音频 packet 进入 `AudioDecoder`，解码后统一重采样到 2 声道、44100Hz、S16 PCM；视频 packet 进入 `VideoDecoder`，解码后统一转成 RGBA 帧，方便 OpenGL 上传纹理。

解码后的音频和视频都会回调到 `MainWindow`。如果正在录制，就先写入 `RecordSession`；同时也推给 `AVSynchronizer`。同步器以音频时钟为主时钟，利用 `QAudioSink::processedUSecs()` 推算当前播放时间。视频帧放入队列后，定时器每 16ms tick 一次，判断视频帧时间戳和主时钟的差值：太早就等，刚好就显示，太晚就丢帧。没有音频设备时会降级为视频时钟。

显示部分在 `QOpenGLWidget` 中完成。视频帧被上传成 OpenGL 纹理，灰度、反色、翻转、贴纸都通过 shader uniform 控制。贴纸位置由 OpenCV 人脸关键点估计，并做了平滑和短暂 hold/fade，避免检测抖动。

录制链路复用播放过程中已经解码出的 PCM 和 RGBA 帧。视频编码器把 RGBA 转成 YUV420P 后编码为 H.264，音频编码器把 S16 PCM 转成 AAC 所需格式，并用 FIFO 攒够 frame size。最后通过 muxer 统一写入 MP4，并注意把 packet 时间戳 rescale 到输出流 time base。

## 4. 核心链路

1. 用户导入文件或输入网络 URL。
2. `MainWindow::startPlayback` 停掉旧 reader，创建 `FileReader`，打开输入源，启动播放。
3. `DeMuxer::Open` 通过 `avformat_open_input` 和 `avformat_find_stream_info` 找到音视频流。
4. `DeMuxer::ThreadLoop` 在后台读取 packet，并按 stream index 分发给音频或视频解码器。
5. `AudioDecoder` 使用 `avcodec_send_packet/receive_frame` 解码，再用 `SwrContext` 重采样为 S16 PCM。
6. `VideoDecoder` 解码视频帧，再用 `SwsContext` 转成 RGBA。
7. `MainWindow` 收到 decoded samples/frame 后，同时写录制链路和播放同步链路。
8. `AVSynchronizer` 以音频时钟为主，按时间戳调度视频帧。
9. `PlayerWidget` 触发 Qt queued update，在 OpenGL 上下文中上传纹理并渲染。

## 5. 面试亮点

### 模块化和接口隔离

项目没有把 FFmpeg 调用全塞在 UI 里，而是用接口把 reader、demuxer、decoder、renderer、recorder 分开。好处是职责清晰，播放、录制、显示、滤镜可以分别替换。

可以说：

> 我把底层音视频处理抽到 AV 模块，UI 层只拿解码后的 frame/sample，不直接感知 FFmpeg 的 AVPacket/AVFrame 细节。这样后续要替换渲染方式、增加网络源、增加录制，都不会牵动 UI 主流程。

### 线程协作和背压

`DeMuxer`、`AudioDecoder`、`VideoDecoder` 都有自己的线程或队列。为了防止读取线程无限制生产 packet，项目用了 `pipelineResourceCount` 和 release callback：packet/frame 生命周期结束时回调，归还一个 pipeline 资源，生产端才继续读。

可以说：

> 这里的思路和后端消息队列限流类似。不是只靠一个无界队列堆数据，而是让消费者释放资源后通知生产者继续生产，这样能控制内存上限，也能降低播放延迟。

### seek 和 flush

拖动进度条时，`DeMuxer` 调用 `av_seek_frame` 和 `avformat_flush`，然后给音频、视频解码器分别发送业务层的 `KFlush` 包。解码器收到后清队列、调用 `avcodec_flush_buffers`，再向上层发 flush frame/sample，让同步器和渲染队列清掉旧数据。

可以主动提到一个 bug：

> 我踩过一个比较典型的坑：FFmpeg 自己的 packet flag 和我定义的业务 flag 发生了语义冲突，关键帧标记被误判成 flush。我后面在封装 `IAVPacket` 后把业务 flags 强制归零，只有 seek 时才显式设置 `KFlush`。

### 音视频同步

同步策略是音频主时钟优先。音频输出后，通过 `QAudioSink::processedUSecs()` 加上音频起始 PTS 作为 master clock。视频帧进入队列后比较 `frameTs - masterClock`：

- 视频太早：暂不显示，等下一次 tick。
- 视频接近当前时间：取出并显示。
- 视频严重落后：丢帧追赶音频。
- 无音频或音频设备不可用：降级为视频时钟。

### 实时渲染和滤镜

视频解码统一转 RGBA，显示层上传到 OpenGL 纹理。灰度、反色、翻转和贴纸都放在 shader 里，通过 uniform 开关控制，避免 CPU 逐像素处理。贴纸使用 OpenCV 人脸检测和 LBF landmark 估计位置，并做 smoothing，减少抖动。

### 录制链路

录制不是简单保存原始输入 packet，而是复用解码后的 PCM/RGBA，因此录到的是当前播放链路里的内容。视频编码 H.264，音频编码 AAC，最后 MP4 mux。录制开始时间以第一帧视频时间戳为 epoch，音频如果早于这个时间会按 sample 数裁掉前导部分，避免音画起点不一致。

## 6. 高频追问和回答

### Q1：FFmpeg 解码大概流程是什么？

回答：

先 `avformat_open_input` 打开文件或网络流，再 `avformat_find_stream_info` 找到流信息。根据 stream 的 codec parameters 创建 codec context，找到 decoder 后 `avcodec_open2`。读取时 `av_read_frame` 拿 packet，按 stream index 分发给音频或视频解码器。解码时使用 `avcodec_send_packet` 和 `avcodec_receive_frame`，音频再 swr 重采样，视频再 sws 转像素格式。

### Q2：为什么视频统一转 RGBA？

回答：

这是为了简化显示层。不同视频源可能是 YUV420P、NV12 等格式，如果直接渲染就需要多个 shader 或多纹理采样。项目里先统一转成 RGBA，OpenGL 侧只需要上传一张纹理。代价是 CPU 转换和内存带宽会更高，生产优化可以改成 YUV shader 直渲或硬件解码。

### Q3：为什么音频统一重采样到 44100Hz、S16、双声道？

回答：

为了和 Qt 音频输出、录制编码链路使用统一格式，减少运行时分支。真实生产里最好根据输出设备 preferred format 或输入流格式动态协商，避免不必要重采样。

### Q4：如何避免播放时内存无限增长？

回答：

有两层控制。第一层是 demuxer 到 decoder 的 pipeline resource count，只有 packet/frame 被消费释放后才归还资源。第二层是同步器视频队列有最大长度，超过上限会丢旧帧。这个思路类似后端里的有界队列和背压。

### Q5：seek 后怎么避免旧帧继续显示？

回答：

seek 时不只是移动 demuxer 的读指针，还会 flush FFmpeg format context 和 decoder context，并向上层发送 flush 标记。音频输出 reset，视频同步队列清空，渲染层也不会继续消费旧帧。

### Q6：音视频同步怎么做？

回答：

以音频为 master clock，因为人对音频卡顿和漂移更敏感，而且音频设备实际播放进度更接近真实播放时间。视频帧按 PTS 排队，定时 tick 时比较视频帧时间和音频时钟，太早等待，太晚丢帧，接近就显示。没有音频时降级为视频时钟。

### Q7：录制时怎么保证音画时间戳对齐？

回答：

录制开始时先用第一帧有效视频的时间戳作为 recording epoch。视频 PTS 转成相对时间后再按编码器 time base 设置 frame pts。音频如果早于这个 epoch，会按采样率计算需要丢掉多少 sample，再进入 AAC FIFO。muxer 写包前会 `av_packet_rescale_ts` 到输出流 time base。

### Q8：网络播放是真正自己实现协议了吗？

回答：

没有自己实现 HTTP/RTSP/RTMP 协议栈，项目里只是做了 URL 识别、状态管理和 UI 控制，实际读取仍然交给 FFmpeg 的 protocol layer。这是合理取舍，因为重点在播放管线。生产化可以继续做重连、超时、缓冲水位、网络错误分类和指标上报。

### Q9：项目里线程安全怎么保证？

回答：

共享队列和 codec context 都有 mutex 保护。listener 回调用 recursive mutex 防止回调过程中状态变化。flush 逻辑里我注意避免同时长时间持有队列锁和 codec 锁，降低死锁风险。更进一步可以用更清晰的线程模型，比如每个模块固定单线程消息循环，减少跨线程直接调用。

### Q10：你遇到过什么问题，怎么解决？

回答：

一个是 seek 后旧帧残留，我通过 flush 包贯穿 demuxer、decoder、synchronizer 和 renderer 解决。另一个是业务 flags 和 FFmpeg packet flags 冲突，导致关键帧被误判为 flush，后来把业务标志和底层标志隔离。还有无音频流或音频设备不可用时的播放问题，我做了视频时钟 fallback。

### Q11：如果让你继续优化，会做什么？

回答：

第一，补测试和 CI，至少覆盖 URL 判断、时间戳换算、seek flush 状态和 muxer 时间戳。第二，改造资源管理，用 RAII 包装 FFmpeg 原始指针。第三，优化性能，把 RGBA CPU 转换改成 YUV shader 或硬件解码。第四，网络源加超时、重连和缓冲状态。第五，整理 CMake/Conan 依赖，让项目在干净环境稳定构建。

## 7. 后端岗位可迁移能力

这个项目可以重点往后端能力上靠：

- 异步流水线：demux、decode、render、record 类似后端服务里的多阶段数据处理。
- 背压和限流：用有限资源控制生产速度，避免内存被队列打爆。
- 状态机：播放、暂停、停止、seek、录制、网络错误都是状态切换。
- 时间戳和一致性：录制和同步要处理不同 time base，类似分布式系统里统一时间语义。
- 资源生命周期：FFmpeg context、packet、frame、OpenGL texture、线程都需要明确 owner 和释放时机。
- 可观测性：项目已有调试日志和 UI 摘要，但还可以系统化为指标、事件和错误码。

## 8. 可以主动承认的不足

这些不足不要躲，主动说反而显得成熟：

- README 还是模板，没有沉淀使用说明和架构图。
- 当前构建在本机重新配置时遇到 Conan/OpenCV/protobuf debug 依赖问题：`protocd` 缺失，属于依赖 profile/build type 不一致问题。
- `NetworkSession` 目前主要是状态和 URL 识别，网络重连、超时和缓冲还没有深入做。
- 目前大量日志是 `std::cout/std::cerr`，生产环境应该统一日志库和错误码。
- 解码后统一 RGBA/S16 简化了工程，但性能不是最优。
- 测试覆盖不足，尤其是并发、seek、时间戳和录制。
- CMake 里还有历史路径名 `LearnAV`，工程命名和目录命名需要清理。

## 9. 今晚突击顺序

1. 先背熟“30 秒版本”和“3 分钟版本”。
2. 画一遍主链路：`输入源 -> DeMuxer -> Decoder -> MainWindow -> AVSynchronizer -> AudioRender/VideoDisplay`。
3. 重点复习四个问题：线程背压、seek flush、音视频同步、录制时间戳。
4. 准备一个 bug 故事：FFmpeg flags 和业务 flush flags 冲突。
5. 准备一个不足和改进：依赖构建、网络重连、测试、RAII、硬件解码。

## 10. 后端岗位桥接题

### C++ 基础

- 智能指针：这个项目里大量用 `shared_ptr` 传递 packet/frame，用 weak callback 避免 release callback 延长生命周期。可以复习 `unique_ptr`、`shared_ptr`、`weak_ptr` 的适用场景。
- RAII：FFmpeg 的 `AVPacket`、`AVFrame`、codec context 都需要明确释放。项目里有一部分封装，但还可以继续抽成自定义 deleter。
- 虚析构：接口类如果通过基类指针释放子类，析构函数必须是 virtual。
- move/copy：视频帧数据用 `shared_ptr<uint8_t>` 持有 buffer，避免大块 RGBA 数据反复拷贝。

### 并发和操作系统

- 生产者消费者：demuxer 生产 packet，decoder 消费 packet，synchronizer 消费 frame。
- 条件变量：`SyncNotifier` 用 condition variable 唤醒工作线程，要处理虚假唤醒和超时。
- mutex 粒度：flush 时避免长时间同时持有队列锁和 codec 锁，降低死锁风险。
- 原子变量：播放暂停、退出标记等简单状态用 atomic。
- 背压：用有限资源计数控制 pipeline，而不是无限队列。

### 网络和服务端设计

- 当前网络播放依赖 FFmpeg protocol layer，自己只做 URL 支持和状态管理。
- 如果做成后端服务，可以拆成 ingest、decode、transcode、store、notify 几个阶段。
- 网络流生产化要考虑连接超时、断线重连、缓冲水位、慢消费者、限流、指标和告警。
- RTSP/RTMP/HLS 可以只讲理解：RTSP 偏实时控制，RTMP 常见直播推流，HLS 是 HTTP 分片播放。

### 数据库和缓存如果被问到

这个项目本身没有数据库，不要硬编。可以这样答：

> 这个项目没有引入数据库，因为主要状态都在本地内存和文件系统里。如果要做成服务端，比如用户上传视频、转码任务、录制文件管理，我会用数据库记录任务状态、文件元数据和错误信息，用 Redis 做任务队列、分布式锁或热点状态缓存。

### 项目如何改成后端服务

可以说：

1. 接入层提供上传、拉流、查询任务状态 API。
2. 任务层把每个视频处理请求投递到队列。
3. worker 复用当前 FFmpeg 管线做解封装、转码、截图、录制。
4. 存储层把输出文件写对象存储，本地只保留临时文件。
5. 状态层记录任务进度、失败原因、输出地址。
6. 监控层上报处理耗时、失败率、队列积压、转码 FPS。

## 11. 源码定位

- UI 启动播放和控制：`qt/MainWindow.cpp`
- 播放链路组织：`AV/src/Reader/FileReader.cpp`
- 解封装和 seek：`AV/src/Reader/DeMuxer.cpp`
- 音频解码和重采样：`AV/src/Reader/AudioDecoder.cpp`
- 视频解码和 RGBA 转换：`AV/src/Reader/VideoDecoder.cpp`
- 音视频同步：`AV/src/Engine/AVSynchronizer.cpp`
- 音频输出：`AV/src/Engine/AudioRender.cpp`
- OpenGL 显示和滤镜：`AV/src/Display/VideoDisplay.cpp`
- 录制入口：`AV/src/Writer/RecordSession.cpp`
- 编码和封装：`AV/src/Writer/Recorder.cpp`、`AV/src/Writer/VideoEncoder.cpp`、`AV/src/Writer/AudioEncoder.cpp`、`AV/src/Writer/Muxer.cpp`
- 网络状态：`AV/src/Network/NetworkSession.cpp`
