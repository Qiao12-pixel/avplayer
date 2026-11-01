# AVPlayer Python Runtime

The Qt/C++ application owns media state, playback, screenshot, subtitle storage,
audio extraction, and clip export. It sends goals or AI sidebar tasks to this
local Python process. The AI assistant UI lets users choose local models
(Ollama + faster-whisper) or Doubao API (Ark chat + speech recognition).
Both communication paths run in Python; C++ executes media tools only.

Install Python dependencies in the project virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r agent/requirements.txt
```

`AgentBridge` prefers `.venv/bin/python` if present. Local model configuration is
read from `resource/local-model.yaml` for local mode and
`config/secret_key.yaml` for Doubao API mode.
Both Qt sidebars share the selected provider; Qt stores the preference with
`QSettings` and sends it to Python as `AVPLAYER_AI_PROVIDER` at process startup.
Later changes use `provider` / `provider_status` JSON Lines messages. Provider
changes are rejected while a task is running and the UI returns to the active
provider.

Download the local models into the project resource directory:

```sh
bash scripts/download_local_models.sh
```

## Execution

`main.py` reads `avplayer.agent.v1` JSON Lines on stdin. `runtime.py` uses
LangChain's `ChatOllama` adapter for local models or `ChatOpenAI` against the
Ark endpoint through `ark_client.py`. A LangGraph `StateGraph` runs the goal
loop: model -> tool requests -> model -> final answer.
The graph runs in a worker thread so the stdin thread can receive asynchronous
C++ `tool_result` messages. A separate `task` message runs AI sidebar actions.

The Python runtime performs subtitle summarization, semantic search, edit-plan
generation, and transcription. For transcription it first requests the C++
`extract_audio` tool, then runs local Whisper or the Doubao speech API according
to the selected mode, and requests C++ `save_transcript` to write the SRT.
It never directly accesses Qt objects or executes FFmpeg.

Messages use `request_id` to match each `tool_call` to its `tool_result`.
The current runtime accepts one active goal or AI task at a time and limits
an Agent goal to eight tool calls. It does not yet persist graph state across
process restarts.

The `ready` and `provider_status` messages report text-model and speech-model
availability separately. Local speech readiness requires both `config.json`
and `model.bin` in the faster-whisper snapshot. A malformed YAML file produces
a configuration error in the UI rather than terminating the Python process.

Run the Runtime regression tests from the project root:

```sh
PYTHONPATH=agent .venv/bin/python -m unittest discover -s agent/tests -v
```

Model weights in `resource/ollama` and `resource/whisper-small` are ignored by
Git. `config/secret_key.example.yaml` is tracked; `config/secret_key.yaml`
remains local-only. Local mode does not send media or subtitles to the Doubao
APIs. Cloud mode sends the necessary text or audio from Python.

## C++ tool catalog

- `get_media_context`, `get_transcript`, `player_control`, `capture_screenshot`
- `extract_audio`, `save_transcript`, `set_edit_plan`
- `export_clip`, `export_random_clip`

Only user-facing tools are bound to the language model. The internal tools
`get_transcript`, `extract_audio`, `save_transcript`, and `set_edit_plan` remain
available to Python orchestration but are not offered as direct model choices.
Every model-facing tool declares a JSON object schema with an object-valued
`properties` field. The runtime normalizes empty object schemas before binding
tools to Ollama; otherwise Ollama rejects the complete catalog with a schema
400 error before any tool runs.

`agent/tools/` and `agent/schemas/` are reserved for future shared tool
adapters and versioned protocol schemas; they are not independent execution
backends today.
