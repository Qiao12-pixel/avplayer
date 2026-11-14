# Python Agent

只负责LangGraph编排、文本模型调用及MCP工具调用，不包含媒体工具实现或本地媒体执行回退。

- runtime.py：智能体和任务协调。
- mcp_client.py：MCP握手、工具发现、调用、异步任务查询取消。
- session_manager.py：会话/媒体绑定、消息订阅及工具调用身份注入。
- data_client/db/cache：基础设施数据RPC适配，不实现数据库驱动或媒体工具。

工具实现位于独立MCP/src/media，Agent默认连接http://127.0.0.1:8081/jsonrpc，可用AVPLAYER_MCP_URL覆盖。
MCP对媒体文件的访问要求与上传方共享storage。模型工具schema从MCP发现，session/media/job身份由Agent生成和绑定。

```sh
PYTHONPATH=services/agent .venv/bin/python -m avplayer_agent
PYTHONPATH=services/agent .venv/bin/python -m unittest discover -s services/agent/tests -v
```
