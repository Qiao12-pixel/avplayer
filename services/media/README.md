# 媒体内部模块

此目录不再生成独立media_server进程，而是构建avplayer_media_service静态库，由services/server的avplayer_server链接。
包含MediaSession、FFmpeg管线、H.264/Opus WebRTC输出、播放控制与录制；与HTTP/WS、数据层同进程运行。

streaming_smoke_client仍作为可选独立验证程序保留（非Qt）。
根工程构建：`cmake --build cmake-build-debug --target avplayer_server streaming_smoke_client -j4`。
正常启动使用scripts/dev.sh。不要再手动启动旧build目录残留的media_server二进制。
