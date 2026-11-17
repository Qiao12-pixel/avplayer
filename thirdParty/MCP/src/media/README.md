> 数据库实现已更新：任务仓储、租约、事件发件箱、统计投影和MCP历史统一使用MySQL；旧SQLite说明仅为历史。当前权威配置、迁移步骤和执行隔离语义见 AVPlayer根目录deploy/mysql/README.md。

# MCP原生媒体工具

本模块在thirdParty/MCP内独立实现媒体查询、字幕/计划、抽音轨、剪辑、转码和转写任务；不调用AVPlayer媒体服务或gRPC。

## 代码边界

- media_backend：轻量文件工具、任务提交/查询/取消，以及Rabbit消费者执行槽。
- executor：原生C++工具执行器，通过posix_spawnp调用ffmpeg/ffprobe，不使用shell。
- inference/speech.py：独立Whisper/云端语音推理，仅输出segments。
- ../messaging/JobStore：SQLite WAL任务状态、取消标记、发布发件箱。
- ../messaging/Brokers：Rabbit持久分发和确认、Kafka事件交付。
- ../messaging/EventConsumer：Kafka事件审计消费与幂等保存。
- ../main/mcp_avplayer_tools：10个媒体工具、2个任务管理工具注册。

任务不再放入进程内std::deque。RabbitMQ分发job_id，Kafka记录生命周期事件；任务/结果保存在MCP_JOB_DB。
工作进程通过Rabbit竞争消费；同机消费者需要共享同一任务DB、锁目录和storage。
这是同机多进程执行，不宣称支持SQLite跨主机分布式队列；详见项目根MESSAGING.md。

## 环境

- AVPLAYER_STORAGE_ROOT：媒体目录，session/media/original.*，与上传方共享。
- MCP_MEDIA_ROOT：定位src/media/inference/speech.py。
- AVPLAYER_PROJECT_ROOT：模型/凭据配置根（resource/local-model.yaml、config/secret_key.yaml）。
- AVPLAYER_WORKER_PYTHON：推理解释器，需要faster-whisper/PyYAML，仅转写使用。
- MCP_MEDIA_EXECUTOR：默认与MCP_Server同目录的mcp_media_executor。
- MCP_MEDIA_SLOTS：每进程执行槽，默认2。
- MCP_RABBIT_HOST/PORT/USER/PASSWORD/VHOST/QUEUE：Rabbit连接，默认本机5672及guest。
- MCP_KAFKA_BROKERS/TOPIC：Kafka地址与事件主题，默认127.0.0.1:9092、avplayer.media.events。
- MCP_JOB_DB：默认data/media_jobs.sqlite3；需持久卷保存。
- MCP_JOB_LOCK_DIR：默认data/job-locks，同机多个执行进程共用。

字幕工具保存JSON/SRT，剪辑产物按job_id隔离。任务终态和取消跨进程重启保留；未确认Rabbit消息可重投。
发布/消费至少一次，job_id和执行锁提供幂等管理，不承诺exactly-once或外部模型请求只发生一次。
MCP工具没有多租户鉴权，身份须由可信调用方绑定。

## 构建与验证

新增依赖rabbitmq-c、librdkafka，仍不需要AVPlayer proto或gRPC。
在AVPlayer根运行scripts/build_mcp.sh，生成MCP_Server、mcp_media_executor、mcp_event_consumer。
启动broker后运行tests/media_standalone.py（相对本MCP目录）或AVPlayer/tests/messaging_integration.py。
独立媒体测试默认build/mcp产物，不启动AVPlayer C++后端；现在需要外部消息broker。
