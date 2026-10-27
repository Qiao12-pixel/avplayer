# Agent Tools

Python 只负责规划；播放器能力由 C++ 工具执行。每个工具需要在 C++
`ToolDispatcher` 中注册名称、描述、输入 schema 和执行函数。

Planned tools:

- `transcribe_subtitle`
- `summarize_video`
- `semantic_search`
- `capture_screenshot`
- `export_clip`
- `player_control`

Python 的模型适配器只产生 `tool_call`，不直接执行 shell、FFmpeg 或 Qt
对象操作。
