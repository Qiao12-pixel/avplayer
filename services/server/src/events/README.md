# 后端事件与统计

`PlaybackEvents` 在业务事件发生处生成上传、播放、控制、录制和结果保存事件，并先写 MySQL outbox，再异步发布 Kafka；它不逐帧采集，也不改变实时音视频传输。

`PlaybackConsumer` 是独立可执行程序：将 Kafka 事件去重写入 MySQL 投影，事务成功后提交 offset。HTTP 统计接口读取同一 projection。事件格式、重放与故障语义见根目录 `MESSAGING.md`。
