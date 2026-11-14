# 启动说明

## 构建

依赖：C++17、Qt6、FFmpeg、OpenCV、Opus、gRPC/Protobuf、Boost、nlohmann-json、hiredis、MySQL/MariaDB C客户端及ZooKeeper C客户端。

```sh
.venv/bin/python -m pip install -e 'services/agent[dev]'
PYTHON=.venv/bin/python bash scripts/gen_proto.sh
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target AVPlayer media_server web_gateway streaming_smoke_client -j4
npm --prefix apps/web install
scripts/build_mcp.sh --install-deps  # 首次安装Conan依赖；已有依赖时省略--install-deps
```

## 开发启动

```sh
scripts/dev.sh
./run_avplayer.sh
# Qt远程模式：
AVPLAYER_STREAMING_MODE=1 ./run_avplayer.sh
```

脚本启动Python Agent、自建C++ MCP、C++ gateway、media_server与Vite；不再启动AVPlayer工具Worker。
已有MCP时用 `--no-mcp`，配置 `AVPLAYER_MCP_URL=http://host:8081/jsonrpc`。
MCP须配置相同AVPLAYER_STORAGE_ROOT（默认本项目storage），MCP_MEDIA_ROOT指向MCP仓库，AVPLAYER_PROJECT_ROOT指向模型配置根目录。
AVPLAYER_WORKER_PYTHON是MCP模型推理解释器（保留旧环境变量名），不是AVPlayer Worker服务。
MCP模型推理需要faster-whisper/PyYAML，参见thirdParty/MCP/src/media/README.md。

| 端口 | 入口 |
|---|---|
| 50061 | Python Agent gRPC |
| 50062 | C++ media gRPC |
| 50065 | C++ gateway HTTP/WS |
| 50066 | C++上传、数据和媒体会话路由 |
| 8081 | 自建MCP /jsonrpc |
| 5173 | Vite |

## 验证

```sh
PYTHONPATH=services/agent .venv/bin/python -m unittest discover -s services/agent/tests -v
.venv/bin/python tests/cpp_gateway_integration.py
.venv/bin/python tests/mcp_independent_agent.py
.venv/bin/python thirdParty/MCP/tests/media_standalone.py
npm --prefix apps/web run build
```

MCP独立测试不启动AVPlayer后端；真实Agent转写测试只启动Agent和MCP。
ZooKeeper媒体均衡配置见deploy/ZOOKEEPER.md。Compose仍需可达的外部MCP，设置deploy/.env中的AVPLAYER_MCP_URL；容器部署尚未完整实测。

原MCP config目录现已原样复制至thirdParty/MCP/config，全部由根.gitignore忽略。开发脚本默认读取其中server.json，可用AVPLAYER_MCP_CONFIG指定其他配置；不再生成替代配置。配置文件内容保持不变，启动地址/端口仍由脚本命令行参数指定。

## 非Qt全链路验证

```sh
AVPLAYER_WEB_HTTP=1 scripts/dev.sh
.venv/bin/python tests/full_stack_no_qt.py --report /tmp/avplayer-full-stack-report.json
```

AVPLAYER_WEB_HTTP=1仅切换Vite为本机HTTP，默认仍HTTPS。localhost可用于WebRTC开发；远程部署仍须安全上下文。
此测试经Vite同源HTTP/WS验证上传、播放控制、真实WebRTC接收/录制、转写、摘要、字幕搜索、智能剪辑、转码和Agent工具编排。
它不代替浏览器实际画面、音频播放与截图滤镜的UI验证，也不会验证云端模型凭据。

MCP为普通源码目录，随AVPlayer仓库一起拉取和提交；执行scripts/build_mcp.sh构建，不需要子模块初始化。
