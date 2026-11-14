# MCP Server 架构分析报告

> 生成日期: 2026-06-12 | 语言: C++17 | 代码量: ~9,700 行 | 8 个 GTest 文件

---

## 一、整体架构分层

```
┌─────────────────────────────────────────────────────────────────────┐
│                     传输层 (Transport)                               │
│  HTTP JSON-RPC (httplib)  │  Stdio JSON-RPC                         │
│  :8081/jsonrpc            │  标准输入输出                            │
│  /sse/events /sse/tool_calls                                        │
└───────────────────────────┬─────────────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────────────┐
│                   JSON-RPC 协议层                                    │
│  JsonRpcDispatcher ─ 方法路由 (6 个标准方法)                        │
│  ThreadPool ─ 异步任务调度 (可配置线程数和队列大小)                   │
│  JsonRpcRequest / JsonRpcResponse / JsonRpcError                    │
└───────────────────────────┬─────────────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────────────┐
│                   MCP 协议层 (核心注册中心)                           │
│  McpServer                                                         │
│  ├── Tool   注册中心 (name → Tool + Handler, 线程安全)              │
│  ├── Resource 注册中心 (url → Resource + Provider, 线程安全)        │
│  └── Prompt 注册中心 (name → Prompt + Generator, 线程安全)          │
└───────────────────────────┬─────────────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────────────┐
│                   工具/资源/提示词实现层                              │
│                                                                     │
│  ├── 内置工具 (14 个)   mcp_builtin_tools.cpp   1,903 行           │
│  │    echo / calculate / get_time / get_weather                     │
│  │    read_file / read_multiple_files / list_directory              │
│  │    search_workspace / compare_files / read_code_context          │
│  │    run_command / write_file / generate_image                     │
│  │    query_tool_history                                            │
│  │                                                                  │
│  ├── Agent 业务工具 (12 个)  mcp_job_agent_tools.cpp  431 行       │
│  │    job_save/get_resume_profile / job_save/get_job_profile        │
│  │    job_save/query_match_result                                   │
│  │    interview_save/update/query_records                           │
│  │    application_save/update_status/query                          │
│  │                                                                  │
│  ├── 内置资源 (2 个)   mcp_builtin_resources.cpp                    │
│  │    system://info / config://server                               │
│  │                                                                  │
│  └── 内置提示词 (1 个)   mcp_builtin_prompts.cpp                   │
│       code_review                                                   │
└──────────┬──────────────────────────────────────────────────────────┘
           │
┌──────────▼──────────────────────────────────────────────────────────┐
│                    基础设施层                                         │
│                                                                     │
│  ├── SQLite: sqlite_database / tool_call_history_repository         │
│  ├── Job Agent DB: job_agent_database (5 张业务表)                  │
│  ├── Config: 单例 Config 读取 server.json                           │
│  ├── Logger: spdlog 封装                                            │
│  └── ZooKeeper: zk_service_registry                                 │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 二、数据流

### 2.1 标准 JSON-RPC 请求流

```
Client
  │ POST /jsonrpc {jsonrpc:"2.0", method, params, id}
  ▼
HttpJsonRpcServer (httplib::Server)
  │ 解析 HTTP → 提取 body → JSON 解析
  ▼
JsonRpcDispatcher
  │ ① 解析 JsonRpcRequest (校验 jsonrpc 版本、id)
  │ ② 查方法路由表 (m_handlers_)
  │ ③ 判断是否进入线程池 (pooled_methods 判定)
  ▼  ┌──── 异步 ────┐
     │ ThreadPool   │  ← 配置: pool_size=16, max_queue=128
     │ ④ 调用 handler(params)
     │ ⑤ 返回 json 结果
     └──────────────┘
  ▼
  │ ⑥ 构造 JsonRpcResponse (result 或 error)
  ▼
HttpJsonRpcServer
  │ 序列化为 HTTP Response
  ▼
Client
```

### 2.2 工具调用具体路径

```
tools/call
  │
  ▼
JsonRpcDispatcher.Call("tools/call", {name, arguments})
  │
  ├─ ShouldRunInThreadPool("tools/call") → YES (默认池化)
  │
  ▼
McpServer.GetTool("get_weather", {city:"Beijing"})
  │
  ├─ 查 m_tool_handlers_["get_weather"] → handler
  │
  ├─ handler(json arguments) → ToolResult
  │     │
  │     ├─ (可选) sql::ToolCallHistoryRepository.Insert()
  │     │   记录调用历史
  │     │
  │     └─ 返回 {content: [{type:"text", text:...}], is_error: false}
  │
  └─ ToolResult.to_json() → 返回给 Dispatcher
```

### 2.3 分布式模式数据流 (Worker + Proxy)

```
Client (Python MCP Client)
  │ POST /jsonrpc
  ▼
LoadBalancer (Proxy 模式)
  │ PickBackend() → 平滑加权轮询
  │ ① 计算 current_weight 选出最优后端
  │ ② httplib::Client POST /jsonrpc 转发
  ▼
Worker 1:8081 / Worker 2:8082
  │ (标准 JSON-RPC 处理流)
  ▼
ZooKeeper
  │ /mcp-servers/worker-{host}-{port}-{seq}
  │ 临时节点 + 自动过期
  │ Watch 机制实时通知 Proxy 实例变化
  └─ ChildWatcher → OnChildChanged → 刷新后端列表
```

---

## 三、核心类职责

### 3.1 MCP 协议层 (mcp/)

| 类/文件 | 职责 |
|---------|------|
| `mcp_types.h` | MCP 协议类型系统: Tool/ToolResult/ContentItem/Resource/Prompt/ServerCapabilities/InitializeResult |
| `mcp_server.h` | 工具/资源/提示词的三合一注册中心, 线程安全, SSE 事件回调 |

**McpServer 关键设计**:

```cpp
// 三个独立的注册表，各自持锁
unordered_map<string, Tool>  m_tools_;          + mutex
unordered_map<string, Resource> m_resources_;   + mutex
unordered_map<string, Prompt> m_prompts_;       + mutex

// 注册 → 查找 → 调用 三步分离的单一路径
RegisterTool(tool, handler) → ListTools() / GetTool(name, args)
```

### 3.2 JSON-RPC 层 (src/json_rpc/)

| 类/文件 | 职责 |
|---------|------|
| `jsonrpc_types.h` | JSON-RPC 2.0 协议: Request/Response/Error 结构体 |
| `jsonrpc_dispatcher.h` | 方法路由注册 + 线程池调度 |
| `http_jsonrpc.h` | HTTP 传输, httplib Server 封装, SSE 端点 |
| `stdio_jsonrpc_server.h` | 标准输入输出传输 |

**Dispatcher 关键设计**:

```cpp
// handler 类型: params → json result
using handler = function<json(const json& params)>;

// 注册
dispatcher.RegisterHandler("tools/call", handler);

// 调用 (自动判断线程池)
json result = dispatcher.Call("tools/call", params);
// 或异步
future<json> result = dispatcher.CallAsync("tools/call", params);
```

### 3.3 基础设施层 (src/)

| 文件 | 行数 | 职责 |
|------|------|------|
| `config/config.h` | 68 | 单例 Config, 读取 server.json, 类型安全 getter |
| `logger/logger.h` | 187 | spdlog 宏封装, 日志轮转 |
| `sql/sqlite_database.h` | 38 | SQLite3 裸封装: Open/Close/Execute |
| `sql/tool_call_history_repository.h` | 49 | 工具调用历史 CRUD |
| `utils/thread_pool.h` | 79 | 线程池: 有界队列 + Submit 模板 + 异常 |
| `job_agent/job_agent_database.h` | 147 | 5 张业务表的完整 CRUD (Statement 封装) |
| `zk/zk_service_registry.h` | 75 | ZooKeeper 服务注册与发现 |
| `load_balancer/load_balancer.h` | 60 | 平滑加权轮询负载均衡器 |

---

## 四、配置体系

`config/server.json` → `Config::LoadFromFile()` → `MCP_CONFIG` 单例

```
server.port             → GetServerPort()          默认 8081
logging.*               → GetLog*()                文件/等级/轮转/控制台
thread_pool.*           → GetThreadPool*()         线程数/队列/池化方法
job_agent.db_path       → GetJobAgentDbPath()      业务数据库路径
image_generation.*      → GetDoubao/Gemini*()      图片生成 API
load_balancer.*         → GetLbDefaultWeight()     默认权重 1
```

SetDefaults() 为所有缺失字段提供默认值，ValidateConfig() 校验端口范围和日志等级。

---

## 五、三种启动模式

| 模式 | 启动方式 | 行为 |
|------|----------|------|
| **standalone** | `--mode standalone --port 8081` | 原始单机模式，不连接 ZooKeeper |
| **worker** | `--mode worker --port 8081 --zk-hosts localhost:2181` | 启动 HTTP 服务 + 注册到 ZK + 线程池 |
| **proxy** | `--mode proxy --proxy-port 8090 --zk-hosts localhost:2181` | 启动 LB，从 ZK 获取后端列表转发 |

```
standalone:  [HttpServer:8081] ← Client
worker:      [HttpServer:8081] ← Client  +  ZkRegister(/mcp-servers/worker-*)
proxy:       [LoadBalancer:8090] ← ZKWatch → [Worker1] [Worker2] [Worker3]
```

---

## 六、工具注册机制

### 工具注册模式

所有工具注册遵循相同模式:

```
1. 定义 Tool (name, description, input_schema)
2. mcp.RegisterTool(tool, handler)
   → 存入 m_tools_ + m_tool_handlers_
   → mutex 保护
3. 调用时: GetTool(name, args)
   → 查 m_tool_handlers_[name]
   → handler(args) → ToolResult
   → (可选) 记录调用历史
```

### 工具分类

```
内置工具 (14 个) [mcp_builtin_tools.cpp, 1903 行]
├── 基础工具: echo, calculate, get_time
├── 工具类: read_file, read_multiple_files, list_directory
├── 搜索: search_workspace (字面量+正则)
├── 差异: compare_files (LCS 行级 diff)
├── 代码: read_code_context (行号范围)
├── 命令: run_command (白名单)
├── 写入: write_file
├── 外部 API: get_weather (Open-Meteo), generate_image (Doubao/Gemini)
└── 历史: query_tool_history

Job Agent 工具 (12 个) [mcp_job_agent_tools.cpp, 431 行]
├── 简历: job_save_resume_profile, job_get_resume_profile
├── JD:   job_save_job_profile, job_get_job_profile
├── 匹配: job_save_match_result, job_query_match_history
├── 面试: interview_save_record, interview_update_record, interview_query_records
└── 投递: application_save, application_update_status, application_query
```

---

## 七、线程安全设计

### 锁分布

| 组件 | 锁类型 | 保护对象 |
|------|--------|----------|
| McpServer 工具注册 | `mutex` (可重入) | `m_tools_`, `m_tool_handlers_` |
| McpServer 资源注册 | `mutex` | `m_resources_` |
| McpServer 提示词注册 | `mutex` | `m_prompts_` |
| McpServer SSE | `mutex` | `m_sse_callback_` |
| McpServer 历史 | `mutex` | `m_tool_history_repository_` |
| LoadBalancer | `shared_mutex` | `backends_`, `weights_` (读写锁) |
| ZkServiceRegistry | `mutex` | `my_node_path_` |
| JobAgentDatabase | `mutex` (方法级) | Statement 执行 |
| ToolCallHistory | `mutex` | Statement 执行 |
| ThreadPool | `mutex` + `condition_variable` | 任务队列 |
| Config | 无 (只读) | - |

### 线程池配置

```
pool_size: 16
max_queue_size: 128
pooled_methods: ["tools/call", "resources/read", "prompts/get"]
```

耗时操作（工具调用、资源读取）进入线程池异步执行，非池化方法（initialize、list 操作）在主线程直接执行。

---

## 八、外部依赖 (Conan)

| 库 | 用途 | 版本 |
|------|------|------|
| `nlohmann_json` | JSON 序列化/反序列化 | - |
| `spdlog` | 日志系统 + 轮转 | - |
| `httplib` | HTTP Server + Client | - |
| `CURL` | 天气查询 API / 图片生成 API | - |
| `SQLite3` | 工具历史 + 业务数据持久化 | - |
| `GTest` | 单元测试框架 | - |
| `ZooKeeper C` | 服务注册与发现 (系统安装) | 3.9.5 |

---

## 九、目录结构

```
mcp/
├── mcp_types.cpp/h        MCP 协议类型 + JSON 序列化
├── mcp_server.cpp/h       注册中心 + 工具/资源/提示词管理

src/
├── config/config.cpp/h    单例配置 (server.json)
├── logger/logger.cpp/h    日志宏封装 (spdlog)
├── json_rpc/
│   ├── jsonrpc_types.h    JSON-RPC 2.0 数据结构
│   ├── jsonrpc_dispatcher.cpp/h  方法路由 + 线程池调度
│   ├── jsonrpc_serialization.cpp/h  Request/Response 序列化
│   ├── http_jsonrpc.cpp/h       HTTP 传输 + SSE 端点
│   └── stdio_jsonrpc_server.cpp/h  stdio 传输
├── main/
│   ├── mcp_server_main.cpp     入口 (参数解析 + 模式分发)
│   ├── server_app.cpp/h        服务装配 (ConfigureMcpServer + CreateDispatcher)
│   ├── mcp_builtin_tools.cpp/h 14 个内置工具
│   ├── mcp_builtin_resources.cpp/h 2 个内置资源
│   ├── mcp_builtin_prompts.cpp/h 1 个内置提示词
│   ├── mcp_job_agent_tools.cpp/h 12 个业务工具
├── job_agent/
│   ├── job_agent_database.cpp/h 5 张业务表 CRUD (Statement 封装)
├── sql/
│   ├── sqlite_database.cpp/h       SQLite3 裸封装
│   └── tool_call_history_repository.cpp/h 调用历史
├── utils/
│   └── thread_pool.cpp/h    线程池 (有界队列 + Submit 模板)
├── zk/
│   ├── zk_service_registry.cpp/h   ZooKeeper 注册发现
├── load_balancer/
│   ├── load_balancer.cpp/h   平滑加权轮询 LB + 健康检查

cmake/
├── FindZooKeeper.cmake      CMake 查找模块
├── CMakeLists.txt           主构建配置
└── config/server.json       服务配置
```

---

## 十、关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| **网络传输** | httplib (C++ header-only) | 零外部依赖启动, 天然支持 HTTP/HTTPS/SSE |
| **JSON 解析** | nlohmann_json | 现代 C++ JSON API, header-only |
| **MCP 注册中心** | 三个独立 map + mutex | Tool/Resource/Prompt 访问模式不同, 独立锁减少竞争 |
| **线程池** | 自定义有界队列 ThreadPool | 比 std::async 可控, 支持队列满时快速失败 |
| **SQLite 封装** | 裸 sqlite3 C API + Statement 类 | 比 ORM 更可控, 预编译 Statement 防注入 + 高性能 |
| **负载均衡** | 平滑加权轮询 (Nginx 算法) | 支持异构权重, 避免同一时间全部涌向高权重节点 |
| **服务发现** | ZooKeeper 临时节点 | 原生 session 过期自动清理, 无需心跳保活 |
| **配置** | 单一 server.json + 单例 Config | 所有配置集中管理, 启动时一次性加载 |
| **启动模式** | standalone/worker/proxy | 同一二进制三种行为, 开发和部署环境一致 |
| **工具历史** | SQLite 异步写入 | 工具调用失败不影响主流程 (catch + warn) |
