# AVPlayer 当前架构

## 进程与职责

| 单元 | 位置 | 职责 |
|---|---|---|
| Qt / Web 客户端 | apps/desktop、apps/web | 展示、播放和用户交互 |
| C++ 网关/数据服务 | services/gateway | HTTP/WebSocket、上传下载、Redis/MySQL、媒体会话路由 |
| C++ 媒体服务 | services/media | FFmpeg编解码、同步、H.264/Opus WebRTC推流、播放录制 |
| Python Agent | services/agent | LangGraph编排、模型调用、MCP工具调用 |
| 独立 C++ MCP | thirdParty/MCP/src/media | 媒体查询、字幕读写、计划保存、抽音轨、剪辑、转码、转写任务 |

Agent工具链为：Agent→MCP HTTP /jsonrpc→MCP本地C++工具/执行器→FFmpeg或独立模型推理。
AVPlayer不再包含这些工具实现、专属task_worker、task_executor或推理模块，也没有工具gRPC转发回退。
MCP内置本地有界任务队列、状态查询、取消和超时；不是AVPlayer媒体服务负载均衡的一部分。

## 播放与上传

Web→gateway :50065 HTTP/WS→media路由 :50066→media_server :50062。
Qt上传到gateway :50066，Agent通信到 :50061，远程播放默认也经gateway :50066。
实际音视频由选中的media_server经WebRTC直接发到客户端，网关不转发媒体包。
上传文件由gateway保存并通知Agent绑定session/media；MCP读取同一媒体目录。
Qt本地播放使用libs/media-core，不需要远程媒体服务。

## 数据

C++ gateway继续拥有上传元数据、任务结果记录及Redis/MySQL接口；这是项目基础设施，不是Agent工具执行。
MCP媒体工具仅维护共享目录中的JSON/SRT/计划与导出文件，不使用AVPlayer缓存；两者没有字幕缓存同步保证。
共享storage布局为session/media/original.*及产物。MCP必须挂载同一目录，才能访问上传内容。
Agent媒体工具不依赖AVPlayer数据/媒体/Worker服务；Agent的最终任务记录仍可通过内部数据RPC写入，失败可降级。

## 负载均衡

media_server通过ZooKeeper临时节点注册，gateway通过Watch和定期核对发现实例。
新播放按本网关活跃会话数最少分配，同负载轮换；后续控制固定原实例。
实例失效清理会话，需要客户端重协商；未配置ZooKeeper时沿用静态媒体地址。
当前是单网关内路由，不支持跨网关共享会话或无缝迁移。

## 构建与协议

根CMake装配proto、media-core、desktop、media、gateway；MCP独立构建，不引用AVPlayer源码或proto。
proto保留上传、Agent消息、播放信令/控制以及内部数据接口；专属工具/Worker协议已移除。
内部gRPC没有鉴权/TLS，文件共享部署和跨主机WebRTC需按目标环境配置。


## 第三方集成

thirdParty/MCP作为Git子模块，作为独立进程构建运行，不直接add_subdirectory混入AVPlayer目标。
Git子模块地址见.gitmodules，主仓库只记录提交指针；私有配置和构建缓存不提交。
scripts/build_mcp.sh构建到build/mcp，scripts/dev.sh默认使用该产物。
可用AVPLAYER_MCP_ROOT、AVPLAYER_MCP_BUILD_DIR、AVPLAYER_MCP_BIN覆盖路径，或--no-mcp连接外部服务。

MCP为Git子模块，首次检出请执行 `git submodule update --init --recursive`，再执行scripts/build_mcp.sh。子模块版本更新需先推送MCP提交，再提交AVPlayer中的gitlink。
