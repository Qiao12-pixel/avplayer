> 本目录是AVPlayer纳入的第三方源码快照，来源与本地修订见 [UPSTREAM.md](UPSTREAM.md)。
> 在AVPlayer根目录用 scripts/build_mcp.sh 构建，默认产物位于build/mcp。私有配置不随模块分发。

# MCP Server

这是一个使用 C++17 实现的 MCP Server 示例项目，当前支持 HTTP JSON-RPC、stdio JSON-RPC、SSE 工具调用事件、线程池调度、SQLite 工具调用历史记录，以及内置 Tools / Resources / Prompts。

项目目标是把 MCP 协议层、JSON-RPC 传输层、业务工具注册和后续扩展点拆开，方便继续加入更多工具、Provider、数据库能力和客户端示例。

## 功能概览

- MCP 初始化、Tools、Resources、Prompts 基础协议类型
- HTTP JSON-RPC endpoint：`/jsonrpc`
- stdio JSON-RPC server
- SSE endpoint：
  - `/sse/events`：服务状态流
  - `/sse/tool_calls`：工具调用开始、完成、错误事件
- 可配置线程池 dispatcher
- SQLite 工具调用历史记录
- 内置工具：
  - `echo`
  - `calculate`
  - `get_time`
  - `get_weather`
  - `read_file`
  - `list_directory`
  - `write_file`
  - `generate_image`
- Python + Ollama MCP demo client
- Python 多 Agent orchestration demo
- GoogleTest 单元测试

## 项目结构

```text
.
├── CMakeLists.txt
├── conanfile.txt
├── config/
│   └── server.json
├── mcp/
│   ├── mcp_types.*
│   └── mcp_server.*
├── src/
│   ├── main/
│   │   ├── mcp_server_main.cpp
│   │   ├── server_app.*
│   │   ├── mcp_builtin_tools.*
│   │   ├── mcp_builtin_resources.*
│   │   └── mcp_builtin_prompts.*
│   ├── json_rpc/
│   │   ├── jsonrpc_dispatcher.*
│   │   ├── jsonrpc_serialization.*
│   │   ├── http_jsonrpc.*
│   │   └── stdio_jsonrpc_server.*
│   ├── config/
│   │   └── config.*
│   ├── logger/
│   │   └── logger.*
│   ├── sql/
│   │   ├── sqlite_database.*
│   │   └── tool_call_history_repository.*
│   └── utils/
│       └── thread_pool.*
├── client/
│   ├── agent_runtime/
│   ├── tests/
│   ├── ollama_mcp_demo.py
│   └── run_multi_agent.py
└── tests/
    ├── builtin_tools_test.cpp
    ├── config_test.cpp
    ├── jsonrpc_test.cpp
    ├── http_jsonrpc_test.cpp
    ├── logger_test.cpp
    ├── mcp_server_test.cpp
    ├── mcp_types_test.cpp
    └── thread_pool_test.cpp
```

核心分层：

- `mcp/`：MCP 协议对象、Server 注册中心、工具/资源/提示词调用入口。
- `src/json_rpc/`：JSON-RPC 请求响应类型、序列化、dispatcher、HTTP/stdio 传输。
- `src/main/`：程序入口、Server 装配、内置 Tools / Resources / Prompts 注册。
- `src/sql/`：SQLite 封装和工具调用历史仓库。
- `src/config/`：读取并校验 `config/server.json`。
- `src/utils/`：线程池。

## 依赖

项目使用 Conan 管理第三方依赖：

- `spdlog`
- `nlohmann_json`
- `cpp-httplib`
- `libcurl`
- `gtest`
- `sqlite3`

当前 CMake 配置使用 C++17，并在 `CMakeLists.txt` 中设置了 `CMAKE_OSX_ARCHITECTURES arm64`，主要面向 Apple Silicon 环境。如果在其他架构构建，需要按本机环境调整。

## 构建

推荐使用当前项目的 Debug 构建目录：

```bash
conan install . --output-folder=cmake-build-debug --build=missing -s build_type=Debug
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

运行测试：

```bash
ctest --test-dir cmake-build-debug --output-on-failure
```

## 配置

主配置文件为 `config/server.json`：

```json
{
  "server": {
    "port": 8080
  },
  "logging": {
    "log_file_path": "../logs/server.log",
    "log_level": "debug",
    "log_file_size": 52428800,
    "log_file_count": 5,
    "log_console_output": true
  },
  "thread_pool": {
    "size": 16,
    "max_queue_size": 128,
    "pooled_methods": [
      "tools/call",
      "resources/read",
      "prompts/get"
    ]
  },
  "image_generation": {
    "default_provider": "doubao",
    "doubao": {
      "api_key": "YOUR_DOUBAO_ARK_API_KEY",
      "model": "doubao-seedream-4-5-251128",
      "api_url": "https://ark.cn-beijing.volces.com/api/v3/images/generations"
    },
    "gemini": {
      "api_key": "",
      "model": "gemini-3.1-flash-image-preview"
    }
  }
}
```

说明：

- `server.port`：HTTP 服务端口。
- `logging`：日志文件、等级、轮转大小和控制台输出。
- `thread_pool.size`：线程池工作线程数量。
- `thread_pool.max_queue_size`：线程池最大排队任务数。
- `thread_pool.pooled_methods`：进入线程池执行的 JSON-RPC 方法。
- `image_generation.default_provider`：`generate_image` 默认使用的生图 provider。
- `image_generation.doubao`：豆包 Ark 生图 API 配置。

如果要上传 GitHub，不要提交真实 API Key。即使 `.gitignore` 已经写了 `/config/server.json`，如果该文件之前已经被 Git 跟踪，还需要执行：

```bash
git rm --cached config/server.json
```

之后保留本地 `config/server.json` 自用，仓库里建议只放不含密钥的示例配置。

## 运行

HTTP 模式：

```bash
./cmake-build-debug/MCP_Server --mode http --config config/server.json --port 8080
```

stdio 模式：

```bash
./cmake-build-debug/MCP_Server --mode stdio --config config/server.json
```

同时启动 HTTP 和 stdio：

```bash
./cmake-build-debug/MCP_Server --mode both --config config/server.json --port 8080
```

查看参数：

```bash
./cmake-build-debug/MCP_Server --help
```

## JSON-RPC 示例

初始化：

```bash
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {},
      "clientInfo": {
        "name": "demo-client",
        "version": "0.1.0"
      }
    }
  }'
```

列出工具：

```bash
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/list",
    "params": {}
  }'
```

调用工具：

```bash
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "calculate",
      "arguments": {
        "operation": "add",
        "a": 10,
        "b": 20
      }
    }
  }'
```

## 内置 Tools

| Tool | 参数 | 说明 |
| --- | --- | --- |
| `echo` | `message` | 返回输入文本 |
| `calculate` | `operation`, `a`, `b` | 基础四则运算和取模 |
| `get_time` | 无 | 获取当前系统时间 |
| `get_weather` | `city` | 通过 Open-Meteo 获取城市天气 |
| `read_file` | `path`, `max_chars?` | 读取当前工作区下的文本文件，禁止越界到工作区外 |
| `read_multiple_files` | `paths`, `max_chars_per_file?` | 批量读取多个工作区文本文件，返回结构化 JSON 结果 |
| `list_directory` | `path?`, `recursive?`, `max_entries?` | 列出当前工作区目录结构，返回 JSON 文本 |
| `search_workspace` | `query`, `path?`, `mode?`, `case_sensitive?`, `max_results?`, `max_file_size_bytes?` | 在工作区内按字面量或正则搜索文本文件 |
| `compare_files` | `path_a`, `path_b`, `max_chars_per_file?`, `max_diff_lines?` | 比较两个文本文件，返回结构化差异摘要 |
| `read_code_context` | `paths`, `start_line?`, `line_count?`, `include_line_numbers?`, `allowed_suffixes?` | 按行号读取代码上下文，适合源码分析和补丁定位 |
| `run_command` | `command`, `args?`, `cwd?`, `timeout_seconds?`, `max_output_chars?` | 在工作区内运行受限白名单命令，返回结构化执行结果 |
| `query_tool_history` | `tool_name?`, `errors_only?`, `limit?` | 查询 SQLite 中最近的工具调用历史 |
| `write_file` | `path`, `content` | 写入本地文件 |
| `generate_image` | `prompt`, `filename`, `size?`, `provider?` | 调用生图 API，解码 base64 图片并保存到 `generated/images` |

`generate_image` 的提示词由客户端模型提供，服务端只负责：

1. 读取 provider 配置；
2. 发送生图请求；
3. 从 API 响应中提取 base64 图片；
4. 解码为二进制图片数据；
5. 保存到本地 `generated/images`。

新增的复杂工具更适合 Agent 编排和工程分析场景：

1. `read_multiple_files`：减少多次工具往返，适合一次加载配置、源码片段或多份文档。
2. `search_workspace`：让 Agent 先检索再读取，降低盲读整个工作区的成本。
3. `compare_files`：适合代码评审、结果核对、配置差异分析。
4. `read_code_context`：让 Agent 精确读取某段源码，而不是整文件全文灌进去。
5. `run_command`：补上最小可控的命令执行能力，适合 `pwd`、`ls`、`cat`、`head`、`tail`、`wc` 这类只读检查。
6. `query_tool_history`：让 Agent 能回看最近工具调用和失败记录，适合恢复、调试和自检。

这些工具都遵守与 `read_file` / `list_directory` 一致的约束：

- 只允许访问 MCP Server 当前工作区内的相对路径；
- 默认按文本文件处理；
- 返回结构化 JSON 文本，便于 Agent 继续消费。

`run_command` 额外限制：

- 只允许白名单命令：`pwd`、`ls`、`cat`、`head`、`tail`、`wc`
- 命令在 MCP Server 当前工作区内执行
- 文件参数会被校验，不能越界到工作区外
- 内建超时和输出截断，避免命令无限挂起或输出过大

## 内置 Resources

| Resource | 说明 |
| --- | --- |
| `system://info` | 返回基础系统信息 |
| `config://server` | 返回部分服务配置 |

## 内置 Prompts

| Prompt | 参数 | 说明 |
| --- | --- | --- |
| `code_review` | `code`, `language` | 生成代码审查提示词 |

## SQLite 工具调用历史

服务启动时会初始化工具调用历史仓库，默认数据库路径：

```text
data/tool_call_history.sqlite3
```

每次工具调用会记录：

- 工具名称
- 参数 JSON
- 是否错误
- 返回结果 JSON
- 错误信息
- 开始时间
- 结束时间
- 耗时毫秒数

SQLite 写入失败不会中断工具调用，只会写入 warn 日志。

## Python + Ollama 客户端

示例客户端在：

```text
client/ollama_mcp_demo.py
```

使用前需要：

1. 启动 MCP Server HTTP 模式；
2. 启动 Ollama；
3. 拉取并确认脚本中配置的模型名称存在。

示例：

```bash
ollama serve
ollama pull qwen3.5:4b
python3 client/ollama_mcp_demo.py
```

如果出现 `localhost:11434 connection refused`，通常是 Ollama 服务没有启动，或本机没有监听 `11434` 端口。

## 多 Agent Demo

项目现在包含一个本地多 Agent 编排层：

```text
client/agent_runtime/
```

该运行时已经开始从手写 `while` 状态机迁移到 LangGraph 风格的图式编排，当前通过轻量本地 graph runtime 驱动 `plan -> select_step -> execute -> review -> finalize` 节点跳转。

固定角色：

- `PlannerAgent`
- `ExecutorAgent`
- `ReviewerAgent`

运行时现在还包含一个本地知识检索层，用于在规划和执行前读取项目文档与源码说明，作为轻量 RAG 上下文注入到提示词中。

CLI 入口：

```text
client/run_multi_agent.py
```

默认配置：

```text
config/agent.json
```

配置字段：

```json
{
  "mcp": {
    "base_url": "http://localhost:8080"
  },
  "ollama": {
    "base_url": "http://localhost:11434",
    "model": "qwen3.5:4b"
  },
  "runtime": {
    "max_plan_steps": 5,
    "max_tool_calls_per_step": 4,
    "max_review_retries": 1
  },
  "knowledge_base": {
    "enabled": true,
    "paths": [
      "README.md",
      "docs",
      "config",
      "client/agent_runtime"
    ],
    "allowed_suffixes": [
      ".md",
      ".txt",
      ".json",
      ".py",
      ".cpp",
      ".h"
    ],
    "chunk_size_chars": 1200,
    "chunk_overlap_chars": 200,
    "top_k": 4
  },
  "storage": {
    "sqlite_path": "data/agent_runtime.sqlite3"
  }
}
```

运行前需要：

1. 启动 MCP Server HTTP 模式；
2. 启动 Ollama；
3. 拉取并确认 `config/agent.json` 中配置的模型已存在；
4. 在仓库根目录执行命令。

运行示例：

```bash
python3 client/run_multi_agent.py \
  --task "查询北京天气并写入 weather.txt" \
  --config config/agent.json \
  --stream
```

恢复任务：

```bash
python3 client/run_multi_agent.py \
  --resume task_xxxxxxxxxxxx \
  --config config/agent.json \
  --stream
```

CLI 输出包含：

- 任务 ID
- 当前 Agent
- 知识检索摘要
- 当前步骤
- 工具调用摘要
- 最终结果

多 Agent SQLite 状态库默认保存在：

```text
data/agent_runtime.sqlite3
```

表结构包括：

- `tasks`
- `task_steps`
- `agent_messages`
- `tool_calls`
- `reviews`

`knowledge_base` 配置说明：

- `enabled`
  - 是否启用本地知识检索
- `paths`
  - 参与索引的文件或目录
- `allowed_suffixes`
  - 允许进入知识库的后缀
- `chunk_size_chars`
  - 每个知识块的最大字符数
- `chunk_overlap_chars`
  - 相邻知识块重叠字符数
- `top_k`
  - 每次为 planner / executor 注入的检索片段数量

Python 单元测试：

```bash
python3 -m unittest discover -s client/tests -p 'test_*.py'
```

## 官方 LangGraph Agent

项目现在还包含一条基于官方 `LangChain + LangGraph + 本地 RAG` 的运行路径。

入口文件：

```text
client/run_langgraph_agent.py
```

推荐使用项目内虚拟环境：

```bash
python3 -m venv .venv
./.venv/bin/python -m pip install -U -r requirements-langgraph.txt
```

运行示例：

```bash
./.venv/bin/python client/run_langgraph_agent.py \
  --task "根据项目文档说明 PlannerAgent 的职责，用中文回答，不要写文件" \
  --config config/agent.json
```

这条官方运行路径的组成是：

- `LangChain`
  - 动态封装 MCP tools
- `LangGraph`
  - 官方 `StateGraph`、`ToolNode` 和 SQLite checkpoint
- `RAG`
  - 复用本地 `knowledge_base.py` 作为项目知识注入层

当前官方 LangGraph 图节点：

- `retrieve_plan`
- `planner`
- `select_step`
- `retrieve_step`
- `executor`
- `tools`
- `review`
- `finalize`

默认 checkpoint 数据库：

```text
data/langgraph_agent.sqlite3
```

当前官方 LangGraph 入口更适合：

- 文档问答
- 规划-执行-评审式多步骤任务
- 基于项目知识的解释类任务
- 需要 checkpoint/thread id 的实验性 Agent 对话

## Web 聊天页

项目现在还包含一个本地 Web 聊天页，用于通过浏览器和官方 LangGraph Agent 交互。

入口文件：

```text
client/web_chat_server.py
```

前端页面：

```text
client/web/chat.html
```

启动方式：

```bash
PYTHONPATH=client ./.venv/bin/python client/web_chat_server.py \
  --config config/agent.json \
  --host 127.0.0.1 \
  --port 8765
```

打开地址：

```text
http://127.0.0.1:8765
```

当前页面能力：

- 浏览器聊天输入框
- 基于 `thread_id` 的连续对话
- 右侧展示当前 thread 的 LangGraph state
- 底层调用官方 `LangGraph + LangChain + 本地 RAG + MCP tools`

## 后续优化方向

- 把 `config/server.json` 拆成可提交的 example 配置和本地私有配置。
- 给 `generate_image` 抽象 provider 接口，进一步拆分豆包、Gemini 等实现。
- 增加工具调用历史查询工具，例如 `list_tool_history`。
- 为 `write_file` 增加路径白名单，避免任意路径写入。
- 给 HTTP Server 增加鉴权或本地访问限制。
- 增加数据库迁移版本表，方便后续 schema 演进。

## 独立音视频工具

MCP 现在内置一份独立 C++ 媒体工具实现，不再是 AVPlayer gRPC 适配器，也不要求 AVPlayer 源码参与构建。
`-DMCP_ENABLE_AVPLAYER=ON` 编译10个媒体工具、2个任务管理工具和原生 mcp_media_executor。
工具实现位于 src/media，注册位于 src/main/mcp_avplayer_tools.cpp。

Agent→MCP→本地C++媒体工具/任务队列→ffmpeg或模型推理，工具链不访问AVPlayer媒体服务、网关和Worker。
配置、能力边界与独立测试见 [src/media/README.md](src/media/README.md)。
