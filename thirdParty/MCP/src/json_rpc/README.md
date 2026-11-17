# JSON-RPC 接入

`http_jsonrpc.*` 将 HTTP POST 映射为 MCP 请求，`stdio_jsonrpc_server.*` 提供标准输入/输出传输；两者共用 dispatcher、类型定义和序列化代码。dispatcher 将工具调用转给 `main/` 注册的工具实现。

该层只处理传输和 JSON-RPC 协议，不执行媒体任务或直接访问业务表。任务工具通过 `messaging/` 与 MySQL/RabbitMQ/Kafka 交互。
