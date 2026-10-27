# AVPlayer Agent Runtime

`agent/` is the Python orchestration boundary. It communicates with the Qt/C++
client through JSON Lines on stdin/stdout and uses protocol version
`avplayer.agent.v1`.

## Message Types

- `goal`: user intent sent from Qt to Python.
- `status`: lifecycle updates from Python to Qt.
- `plan`: planned steps from Python to Qt.
- `tool_call`: a requested C++ tool execution.
- `tool_result`: the result of a C++ tool execution.
- `error`: protocol or task error.

`main.py` now calls the Ark Chat API through `ark_client.py`, requires the model
to return strict JSON commands, and continues the loop after each `tool_result`.
FFmpeg, OpenGL and Qt-owned operations remain C++ tools.

The current C++ catalog contains:

- `get_media_context`
- `player_control`
- `capture_screenshot`
- `transcribe_subtitle`
- `summarize_video`
- `semantic_search`
- `smart_edit`
- `export_clip`
- `export_random_clip` (directly exports a randomly located clip of `duration_ms`)

The first three return synchronously. The media analysis tools start the existing
`AIAssistantService` task and send the result back after its Qt signal completes.
