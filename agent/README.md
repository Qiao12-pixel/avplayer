# AVPlayer Python Runtime

The Qt/C++ application owns media state, playback, screenshot, subtitle storage,
audio extraction, and clip export. It sends goals or AI sidebar tasks to this
local Python process. All requests to Ark chat models and Doubao speech APIs
are made here; C++ does not hold a model networking client.

Install Python dependencies in the project virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r agent/requirements.txt
```

`AgentBridge` prefers `.venv/bin/python` if present. `config/secret_key.yaml`
remains the only credential source and is read by `runtime.py`.

## Execution

`main.py` reads `avplayer.agent.v1` JSON Lines on stdin. `runtime.py` uses
LangChain's `ChatOpenAI` adapter with the configured Ark endpoint. A LangGraph
`StateGraph` runs the goal loop: model -> tool requests -> model -> final answer.
The graph runs in a worker thread so the stdin thread can receive asynchronous
C++ `tool_result` messages. A separate `task` message runs AI sidebar actions.

The Python runtime performs subtitle summarization, semantic search, edit-plan
generation, and speech API requests. For transcription it first requests the
C++ `extract_audio` tool, reads and deletes the temporary MP3, sends audio to
the Doubao speech API, then requests C++ `save_transcript` to write the SRT.
It never directly accesses Qt objects or executes FFmpeg.

Messages use `request_id` to match each `tool_call` to its `tool_result`.
The current runtime accepts one active goal or AI task at a time and limits
an Agent goal to eight tool calls. It does not yet persist graph state across
process restarts.

## C++ tool catalog

- `get_media_context`, `get_transcript`, `player_control`, `capture_screenshot`
- `extract_audio`, `save_transcript`, `set_edit_plan`
- `export_clip`, `export_random_clip`

`agent/tools/` and `agent/schemas/` are reserved for future shared tool
adapters and versioned protocol schemas; they are not independent execution
backends today.
