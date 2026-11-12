# 当前架构基准（实测）

所有数字由 `scripts/bench_metrics.py` 与 `streaming_smoke_client` 现场采集，
不引用历史文档中的旧指标。

## 环境

| 项 | 值 |
| --- | --- |
| 机器 | Apple M5，macOS 26.x（Darwin 27.0.1） |
| 构建 | CMake `Debug`（`cmake-build-debug`），未做 Release 优化 |
| FFmpeg / x264 | 9.0.2，`veryfast` + `zerolatency` |
| 拓扑 | agent(50161) + media_server(50162) + gateway(50165) + 独立测量客户端，全部本机 loopback |
| 素材 | `resource/bench-1080p60.mp4`：1920×1080/60fps，H.264 + AAC，40s（`testsrc2` + 1kHz 正弦） |

## 采样方式

- **推流帧率**：`WebRtcFrameSink` 内置统计（`AVPLAYER_STREAM_STATS=1`），每 5 秒输出
  一次 `fps` / `mbps`，统计的是真正 `sendFrame` 出去的编码帧。
- **接收端**：`streaming_smoke_client` 作为真实 WebRTC 接收方（H.264 + Opus），
  记录 ICE 建连耗时、首帧到达、包速率、码率与包间隔。
- **控制面**：同一次会话内，C++ 客户端直连 gRPC 发 30 次 `Control`；Python 侧
  通过网关 `POST /control` 发 120 次。
- **转写**：`resource/input.mp4`（132s，含语音）走 `transcribe_subtitle` 全流程
  （ffmpeg 抽音轨 → faster-whisper small/int8）。
- **导出**：`export_video` 截取 40s 素材的前 20s（`-ss` + 重新编码）。

## 结果

| 指标 | 数值 |
| --- | --- |
| 服务端编码推流（1080p60 输入，含录制负载） | **59.55 / 60.00 fps** |
| 推流码率 | 4.09 / 4.00 Mbps |
| ICE 建连 | 69 ms |
| 建连后首帧到达 | 12.3 ms |
| 客户端接收 | 4.11 Mbps，1027 包/秒，音频 54 kbps |
| 包间隔 | p50 0.01 ms，p95 12.9 ms |
| 控制面（C++ 直连 gRPC） | p50 **0.11 ms**，p95 0.15 ms |
| 控制面（浏览器路径：HTTP → 网关 → gRPC） | p50 **1.07 ms**，p95 1.89 ms |
| media_server 常驻内存 | 14.6 MB（推流前后无增长） |
| 服务端录制 | 2.08s 输出，1920×1080 H.264 + AAC |
| 转写 132s 视频 | 6.4 s，17 条字幕（RTF 0.05） |
| 片段导出 20s | 5.28 s（3.8× 实时） |

## 本轮由测量发现并修复的两个问题

### 1. 推流被调度节拍限制在 ~46 fps

`MediaSession::TickLoop` 每 16 ms 调一次 `AVSynchronizer::Tick()`，而 `Tick()`
每次最多派发一帧，理论天花板 ≈62 fps，实测 46 fps（1080p 与 720p 相同，说明
瓶颈不在编码分辨率）。把服务端 tick 周期降到 **8 ms** 后达到 59.6–60.0 fps，
`masterClock` 与帧 PTS 重新对齐（11.975s vs 11.983s）。

### 2. 开启录制时的锁顺序反转（死锁）

`StartRecording` 先拿 `m_recordMutex`，再调 `FileReader::GetVideoWidth()`
（内部要 codec 锁）；而解码线程在持有 codec 锁时回调 `OnFileReaderNotifyVideoFrame`
并去拿 `m_recordMutex`。两个方向相对，`record_start` 会永久阻塞。tick 提到 8ms 后
解码线程几乎不空闲，这个潜伏 bug 变成必现。修复方式是**在取 `m_recordMutex`
之前读取尺寸**，消除反向加锁。

## 复现

```sh
# 1) 起服务（隔离端口，避免和 dev.sh 抢）
AVPLAYER_MEDIA_SERVER_ADDRESS=0.0.0.0:50162 AVPLAYER_STREAM_STATS=1 \
  ./cmake-build-debug/services/media/media_server &
AVPLAYER_AGENT_PORT=50161 .venv/bin/python -m avplayer_agent &
AVPLAYER_AGENT_SERVER=localhost:50161 AVPLAYER_MEDIA_SERVER=localhost:50162 \
  AVPLAYER_WEB_GATEWAY_PORT=50165 .venv/bin/python -m avplayer_agent.web_gateway &

# 2) 测量
.venv/bin/python scripts/bench_metrics.py \
  --media-server localhost:50162 --agent localhost:50161 \
  --gateway http://127.0.0.1:50165 \
  --clip resource/bench-1080p60.mp4 --stream-seconds 12 \
  --transcribe --transcribe-clip resource/input.mp4 \
  --json-out docs/bench-current.json
```

## 口径说明

- 所有数字在 **Debug 构建**下取得，Release 预计更高，不应对外宣称 Release 数据。
- 素材是 `testsrc2` 合成画面，纹理细节高于实拍，编码压力偏大；结论「约 60 fps」
  针对该条件。
- 上传吞吐（38 MB / 0.03 s）走 loopback + 页缓存，**不写入简历**，没有代表性。
- 首帧 12.3 ms 是「ICE 建连成功 → 首个 RTP 视频包」，不含浏览器解码渲染。
