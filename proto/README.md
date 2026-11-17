# gRPC / Protobuf 契约

本目录集中维护 AVPlayer 组件之间的 Protobuf 消息和 gRPC service 定义。它是网络接口契约，不承载实现代码或业务持久化逻辑。

`gen_proto.sh` 根据这些定义生成 C++/Python 绑定；生成物由各自构建系统消费。新增或修改字段时应保持兼容编号，不复用已删除字段编号，并同时检查 `services/server`、`services/media`、`services/agent` 的调用方。执行方式见 `scripts/gen_proto.sh` 与 [启动文档](../STARTUP.md)。
