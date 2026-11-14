# 非Qt全项目验证（2026-11-14）

本轮没有构建或启动Qt客户端。环境：Apple M5、macOS、本机服务、Debug C++构建，使用本地Qwen3:4b与Whisper。

## 构建与回归

- C++ gateway、media_server、streaming_smoke_client构建成功。
- thirdParty/MCP独立构建成功，93项CTest通过。
- Python Agent 25项回归测试通过。
- Web TypeScript与Vite生产构建通过。
- dev.sh默认读取thirdParty/MCP/config/server.json，实际启动成功；未改动原配置内容。
- Compose（含discovery profile）配置校验通过。

## 实际流程

通过Vite同源HTTP/WebSocket调用，无模拟模型或工具：

1. 页面/配置可访问，上传23,292,898字节视频并绑定Agent媒体。
2. WebSocket交换SDP/ICE，真实C++接收端收到H.264视频和Opus音频。
3. HTTP查询状态、暂停、Seek、播放成功。
4. 服务端录制MP4并通过HTTP下载成功。
5. Agent→MCP→原生音轨提取→Whisper转写→字幕保存，17条字幕可下载。
6. 本地Qwen完成摘要、字幕语义搜索、智能剪辑计划。
7. MCP按计划导出、转码为360p并下载，ffprobe校验产物。
8. 自然语言Agent调用export_video，实际产生约1秒H.264片段，不只检查最终文本。
9. 网关独立测试通过真实Redis/MySQL读写、会话缓存隔离、上传下载、WS与WebRTC录制。
10. MCP独立测试通过媒体查询、字幕/计划、抽音轨、剪辑、转码、任务幂等、取消与会话隔离。
11. 两媒体实例4路真实播放分配2/2；控制固定路由、重复拒绝、断连清理、SIGKILL摘除、重启注册通过。
12. ZooKeeper故障期间拒绝新播放，已有播放/控制继续；会话过期后恢复与重新注册通过。

报告明细：VALIDATION_NO_QT.json。
复现：启动脚本后运行tests/full_stack_no_qt.py；数据库和故障测试见tests/README.md。

## 本轮修复

发现自然语言模型漏传export_video.end_ms并重复失败：收紧MCP工具schema，区分指定区间与随机时长导出；重测真实Agent导出成功。
增加AVPLAYER_WEB_HTTP=1本机调试选项，默认仍HTTPS；默认HTTPS配置启动与/config代理也已验证（curl使用开发证书忽略校验，不等同浏览器信任验证）。

## 未验证与限制

- 浏览器自动化宿主不可用：未验证真实浏览器解码后的画面/声音、UI点击、截图和滤镜。Web接口和WebRTC接收协议测试不能替代这些。
- Docker守护进程未运行：仅校验Compose，不宣称镜像构建或容器部署跑通。
- 使用本地模型验证，未调用云端语音/文本或图片API，不验证原配置中API key有效性。
- 本轮不是长稳、压力或公网测试，不承诺并发容量、零泄漏或跨主机WebRTC可达性。
