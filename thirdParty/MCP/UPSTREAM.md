# 来源与本地集成

- 来源：用户自建 C++ MCP 项目（原目录 /Users/joe/CLionProjects/MCP）。
- 上游基准提交：13f71257c800b045a7a9c0559e5c758d9369859f。
- 快照包含该提交之后在当前会话实现的独立媒体工具，日期2026-11-14；不是仅该提交的原始归档。
- 集成形式：普通源码目录，由AVPlayer主仓库直接跟踪；不是Git子模块，原外部仓库保留不动。
- 未发现上游LICENSE文件，不新增或推断许可；对外分发前由项目所有者确定授权。
- 排除：私有config/server.json、凭据、虚拟环境、依赖缓存、日志、数据库、构建产物和个人测试文件。
- 本地调整：CMake移除硬编码arm64和必读原构建目录的toolchain，保留独立构建入口。
- 后续更新需显式同步源码和复测，不能将本地补丁覆盖成旧上游实现。

附加本地调整：macOS SDK绝对路径解析；配置测试改用无密钥fixture；独立媒体测试默认使用AVPlayer/build/mcp产物。
依赖包未复制进源码；首次可用Conan安装。当前验证复用本机已有Conan包缓存及临时build目录下的CMakeDeps元数据。

原MCP config目录现已原样复制至thirdParty/MCP/config，全部由根.gitignore忽略。开发脚本默认读取其中server.json，可用AVPLAYER_MCP_CONFIG指定其他配置；不再生成替代配置。配置文件内容保持不变，启动地址/端口仍由脚本命令行参数指定。

全链路验证修订：export_video 的模型schema明确要求start_ms/end_ms，随机导出工具明确要求duration_ms，避免模型漏传区间后反复执行失败。

当前源码包含独立媒体工具及AVPlayer集成修订；config目录由本目录.gitignore忽略。
