# Agent Tools

Python 只负责规划；播放器能力由 C++ 工具执行。每个工具需要在 C++
`ToolDispatcher` 中注册名称、描述、输入 schema 和执行函数。

Current C++ tools:

- `get_media_context`
- `get_transcript`
- `extract_audio`
- `save_transcript`
- `set_edit_plan`
- `capture_screenshot`
- `export_clip`
- `export_random_clip`
- `player_control`

字幕转写、摘要、语义搜索与智能剪辑属于 Python 任务/图节点；媒体工具通过
`tool_call` 交由 C++ 执行。Python 不直接执行 FFmpeg 或操作 Qt 对象。
