"""LangGraph orchestration; all model and speech requests stay in Python."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import threading
import uuid
from pathlib import Path
from typing import Any, Callable, TypedDict

import yaml
from langchain_core.messages import AIMessage, HumanMessage, SystemMessage, ToolMessage
from langchain_ollama import ChatOllama
from langgraph.graph import END, START, StateGraph

from .ark_client import create_ark_model
from .protocol import emit, message
from .settings import project_root


TASK_NAMES = {
    "transcribe_subtitle": "字幕转写",
    "summarize_video": "视频摘要",
    "semantic_search": "语义搜索",
    "smart_edit": "智能剪辑",
}

PYTHON_TOOLS = [
    {"name": "transcode_video", "description": "将已上传视频转码为 H.264/AAC MP4，指定输出高度。", "input_schema": {"type": "object", "properties": {"height": {"type": "integer", "enum": [360, 480, 720, 1080]}}, "required": ["height"]}},
    {"name": "transcribe_subtitle", "description": "为当前本地视频转写音频并生成带时间戳的字幕。", "input_schema": {"type": "object", "properties": {}}},
    {"name": "summarize_video", "description": "仅依据当前视频的字幕生成摘要；需要已有字幕。", "input_schema": {"type": "object", "properties": {}}},
    {"name": "semantic_search", "description": "根据当前视频字幕查找语义相关内容。", "input_schema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "smart_edit", "description": "根据带时间戳的字幕生成剪辑建议。完成后可调用 export_clip。", "input_schema": {"type": "object", "properties": {"instruction": {"type": "string"}}, "required": ["instruction"]}},
]

INTERNAL_CPP_TOOLS = {"get_transcript", "extract_audio", "save_transcript", "set_edit_plan"}
# Tasks that are executed directly by the server-side tool executor rather
# than by the LangGraph model loop (used by the Web client's plan export).
SERVER_TASKS = {"export_plan", "transcode_video"}


def _model_tool_schemas(tools: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Expose only user-facing tools and require an Ollama-compatible object schema."""
    schemas = []
    for item in tools:
        name = item.get("name", "")
        if name in INTERNAL_CPP_TOOLS:
            continue
        parameters = dict(item.get("input_schema") or {})
        if parameters.get("type", "object") != "object":
            raise ValueError(f"工具 {name} 的 input_schema 必须是对象")
        properties = parameters.get("properties", {})
        if not isinstance(properties, dict):
            raise ValueError(f"工具 {name} 的 properties 必须是对象")
        parameters["type"] = "object"
        parameters["properties"] = properties
        parameters.setdefault("required", [])
        schemas.append({"type": "function", "function": {
            "name": name, "description": item.get("description", ""),
            "parameters": parameters,
        }})
    return schemas


class GraphState(TypedDict):
    messages: list[Any]
    steps: int


def _content(response: AIMessage) -> str:
    if isinstance(response.content, str):
        return response.content
    return "".join(part.get("text", "") for part in response.content if isinstance(part, dict))


def _json_object(text: str) -> dict[str, Any]:
    raw = text.strip()
    if raw.startswith("```"):
        raw = raw.split("\n", 1)[-1].rsplit("```", 1)[0].strip()
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError("模型未返回 JSON 对象")
    return value


class AgentRuntime:
    def __init__(self, output: Any, model: Any | None = None,
                 tool_executor: Callable[[str, dict[str, Any], str], dict[str, Any]] | None = None,
                 sink: Callable[[dict[str, Any]], None] | None = None) -> None:
        self.output = output
        self._sink = sink
        self._write_lock = threading.Lock()
        self._condition = threading.Condition()
        self._pending: dict[str, dict[str, Any]] = {}
        self._worker: threading.Thread | None = None
        self._worker_lock = threading.Lock()
        # When set, `_call_cpp` invokes this in-process instead of emitting a
        # tool_call message and blocking on a tool_result reply. This is how
        # the gRPC server (services/agent) runs the former C++-only tools
        # (extract_audio, save_transcript, ...) directly on the server, now
        # that they are plain ffmpeg wrappers with no local-UI dependency.
        self._tool_executor = tool_executor
        self.external_tools = None
        self.tools: list[dict[str, Any]] = []
        root = project_root()
        config_path = root / "config" / "secret_key.yaml"
        self.config, self.cloud_config_error = self._load_config(config_path)
        local_path = root / "resource" / "local-model.yaml"
        self.local_config, self.local_config_error = self._load_config(local_path)
        self.project_root = root
        self.provider = "cloud" if os.environ.get("AVPLAYER_AI_PROVIDER") == "cloud" else "local"
        self._ollama_process: subprocess.Popen[str] | None = None
        self.shutdown_requested = False
        self.model = model
        self._graph = None
        self._active_session = ""

    @staticmethod
    def _load_config(path: Path) -> tuple[dict[str, Any], str]:
        name = f"{path.parent.name}/{path.name}"
        if not path.is_file():
            return {}, f"未找到配置文件：{name}"
        try:
            value = yaml.safe_load(path.read_text(encoding="utf-8"))
        except OSError:
            return {}, f"配置文件无法读取：{name}"
        except yaml.YAMLError as exc:
            mark = getattr(exc, "problem_mark", None)
            location = f"第 {mark.line + 1} 行" if mark is not None else "格式错误"
            return {}, f"YAML 配置格式错误：{name}，{location}"
        if not isinstance(value, dict):
            return {}, f"配置文件应为键值映射：{name}"
        return value, ""

    def local_model_configured(self) -> bool:
        return self.local_config.get("provider", "ollama") == "ollama" and bool(self.local_config.get("model", ""))

    def cloud_model_configured(self) -> bool:
        return bool(self.config.get("doubao_api_key") and self.config.get("doubao_model"))

    def cloud_speech_configured(self) -> bool:
        return bool(self.config.get("doubao_speech_api_key"))

    def provider_status(self) -> dict[str, Any]:
        if self.provider == "cloud":
            model_ready, speech_ready = self.cloud_model_configured(), self.cloud_speech_configured()
            missing = []
            if not model_ready: missing.append("doubao_api_key / doubao_model")
            if not speech_ready: missing.append("doubao_speech_api_key")
            text = "豆包 API 凭据已填写（未验证）。" if not missing else (
                self.cloud_config_error or "缺少豆包配置：" + "、".join(missing))
        else:
            model_ready, speech_ready = self.local_model_available(), self.local_speech_configured()
            missing = []
            if not model_ready:
                missing.append("Ollama/Qwen3" if shutil.which("ollama") else "Ollama 程序")
            if not speech_ready: missing.append("Whisper small")
            text = "本地模型就绪。" if not missing else (
                self.local_config_error or "未就绪：" + "、".join(missing) + "；请运行 scripts/download_local_models.sh")
        return {"provider": self.provider, "model_configured": model_ready,
                "speech_configured": speech_ready, "config_status": text}

    def local_model_available(self) -> bool:
        if not self.local_model_configured(): return False
        try:
            import urllib.request
            host = self._ensure_ollama()
            with urllib.request.urlopen(host + "/api/tags", timeout=2) as response:
                models = json.load(response).get("models", [])
            wanted = str(self.local_config["model"])
            return any(item.get("name") == wanted or item.get("model") == wanted for item in models)
        except Exception:
            return False

    def local_speech_configured(self) -> bool:
        model = Path(str(self.local_config.get("whisper_model_path", "")))
        if not model.is_absolute(): model = self.project_root / model
        return model.is_dir() and any(
            (config.parent / "model.bin").is_file() for config in model.rglob("config.json")
        )

    def _ensure_ollama(self) -> str:
        host = os.environ.get("AVPLAYER_OLLAMA_HOST") or str(
            self.local_config.get("host", "http://127.0.0.1:11435"))
        host = host.rstrip("/")
        models_dir = Path(str(self.local_config.get("models_dir", "resource/ollama")))
        if not models_dir.is_absolute(): models_dir = self.project_root / models_dir
        if self._ollama_process is not None and self._ollama_process.poll() is not None:
            self._ollama_process = None
        if self._ollama_process is None:
            try:
                import urllib.request
                urllib.request.urlopen(host + "/api/tags", timeout=1).close()
            except Exception:
                env = os.environ.copy()
                env["OLLAMA_HOST"] = host.removeprefix("http://")
                env["OLLAMA_MODELS"] = str(models_dir)
                self._ollama_process = subprocess.Popen(["ollama", "serve"], env=env,
                                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                                         text=True)
                for _ in range(30):
                    try:
                        urllib.request.urlopen(host + "/api/tags", timeout=1).close()
                        break
                    except Exception:
                        import time; time.sleep(0.2)
        return host

    def _emit(self, kind: str, request_id: str, session_id: str, **fields: Any) -> None:
        payload = message(kind, request_id, session_id, **fields)
        with self._write_lock:
            if self._sink is not None:
                self._sink(payload)
            else:
                emit(payload, self.output)

    def _model(self) -> Any:
        if self.model is None:
            if self.provider == "cloud":
                self.model = create_ark_model(self.config)
            else:
                if not self.local_model_available():
                    raise RuntimeError("本地 Qwen3 模型未下载；请运行 scripts/download_local_models.sh")
                self.model = ChatOllama(model=str(self.local_config["model"]),
                                        base_url=self._ensure_ollama(), temperature=0,
                                        validate_model_on_init=True, num_ctx=8192,
                                        num_predict=2048, keep_alive="5m")
        return self.model

    def shutdown(self) -> None:
        if self._ollama_process is not None and self._ollama_process.poll() is None:
            self._ollama_process.terminate()
            try:
                self._ollama_process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._ollama_process.kill()
                self._ollama_process.wait(timeout=2)
        self._ollama_process = None

    def set_tools(self, tools: list[dict[str, Any]]) -> None:
        self.tools = tools
        self._graph = None

    def handle(self, data: dict[str, Any]) -> None:
        kind = data.get("type")
        request_id = data.get("request_id", "")
        session_id = data.get("session_id", "")
        if kind == "tool_catalog":
            self.set_tools(data.get("payload", {}).get("tools", []))
            self._emit("status", request_id, session_id, status="tool_catalog_updated", tool_count=len(self.tools))
        elif kind == "provider":
            requested = data.get("payload", {}).get("provider", "local")
            if requested not in ("local", "cloud"):
                self._emit("error", request_id, session_id, error="未知模型方式")
                return
            with self._worker_lock:
                if self._worker is not None and self._worker.is_alive():
                    self._emit("provider_status", request_id, session_id,
                               **{**self.provider_status(), "config_status": "任务执行中，无法切换模型方式。"})
                    return
                if requested == "cloud" and self.provider == "local":
                    self.shutdown()
                self.provider = requested
                self.model = None
                self._graph = None
            self._emit("provider_status", request_id, session_id, **self.provider_status())
        elif kind == "tool_result":
            with self._condition:
                if request_id in self._pending:
                    self._pending[request_id] = data.get("payload", {})
                    self._condition.notify_all()
        elif kind == "shutdown":
            self.shutdown_requested = True
        elif kind in ("goal", "task"):
            with self._worker_lock:
                if self._worker is not None and self._worker.is_alive():
                    self._emit("error" if kind == "goal" else "task_result", request_id, session_id,
                               error="已有任务正在执行，请等待完成。", task=data.get("payload", {}).get("task", ""))
                    return
                self._worker = threading.Thread(target=self._run, args=(kind, data), daemon=True)
                self._worker.start()
        else:
            self._emit("error", request_id or "protocol", session_id, error=f"不支持的消息类型：{kind}")

    def _run(self, kind: str, data: dict[str, Any]) -> None:
        request_id = data.get("request_id", "")
        session_id = data.get("session_id", "")
        try:
            self._active_request_id = request_id
            self._active_session = session_id
            if kind == "task":
                task = data.get("payload", {}).get("task", "")
                if task not in TASK_NAMES and task not in SERVER_TASKS:
                    raise ValueError("未知 AI 任务：" + task)
                result = self._perform(task, data.get("arguments", {}), session_id)
                self._emit("task_result", request_id, session_id, task=task, **result)
            else:
                goal = str(data.get("payload", {}).get("goal", "")).strip()
                if not goal:
                    raise ValueError("任务目标不能为空")
                self._emit("status", request_id, session_id, stage="planning", status="正在规划目标并选择工具…")
                state = self._build_graph().invoke({"messages": [HumanMessage(content=goal)], "steps": 0},
                                                   config={"recursion_limit": 25})
                # A tool-calling model can stop without emitting text (empty
                # content). Ask once more, without tools, so the run ends with
                # a usable answer instead of a blank panel.
                text = _content(state["messages"][-1]).strip()
                if not text:
                    self._emit("status", request_id, session_id, stage="planning",
                               status="正在整理最终结论…")
                    try:
                        reply = self._model().invoke([
                            *state["messages"],
                            HumanMessage(content="请用中文简要总结上面的工具结果，不要再调用任何工具。"),
                        ])
                        text = _content(reply).strip()
                    except Exception:
                        text = ""
                self._emit("final", request_id, session_id,
                           text=text or "模型没有返回文字结论；请查看上方工具执行结果，或换个说法重试。")
        except Exception as exc:
            message = str(exc)
            if "recursion limit" in message.lower():
                message = "任务步骤超过上限（模型反复调用工具）；请缩小目标范围后重试。"
            self._emit("task_result" if kind == "task" else "error", request_id, session_id,
                       error=message, **({"task": data.get("payload", {}).get("task", "")} if kind == "task" else {}))
        finally:
            with self._worker_lock:
                if self._worker is threading.current_thread():
                    self._worker = None

    def _call_cpp(self, tool: str, arguments: dict[str, Any], session_id: str) -> dict[str, Any]:
        if self._tool_executor is not None:
            return self._tool_executor(tool, arguments, session_id)
        request_id = str(uuid.uuid4())
        with self._condition:
            self._pending[request_id] = None  # type: ignore[assignment]
        self._emit("tool_call", request_id, session_id, tool=tool, arguments=arguments)
        with self._condition:
            received = self._condition.wait_for(lambda: self._pending[request_id] is not None, timeout=180)
            result = self._pending.pop(request_id)
        if not received:
            raise TimeoutError(f"工具 {tool} 超过 180 秒未返回")
        return result

    def _require_cpp(self, tool: str, arguments: dict[str, Any], session_id: str) -> dict[str, Any]:
        result = self._call_cpp(tool, arguments, session_id)
        if not result.get("success", False):
            raise RuntimeError(result.get("error", f"工具 {tool} 失败"))
        return result

    def _transcript(self, session_id: str, require_timestamps: bool = False) -> dict[str, Any]:
        value = self._require_cpp("get_transcript", {}, session_id)
        if not value.get("text"):
            raise RuntimeError("请先转写字幕，或在视频旁放置同名 .srt/.txt 文件。")
        if len(value["text"]) > 20000:
            raise RuntimeError("字幕超过 2 万字；当前版本不支持长视频分析。")
        if require_timestamps and not value.get("has_timestamps"):
            raise RuntimeError("智能剪辑需要带时间戳的字幕。")
        return value

    def _perform(self, tool: str, arguments: dict[str, Any], session_id: str) -> dict[str, Any]:
        if tool == "transcribe_subtitle":
            return self._transcribe(session_id)
        if tool not in TASK_NAMES:
            return self._call_cpp(tool, arguments, session_id)

        transcript = self._transcript(session_id, tool == "smart_edit")
        text = transcript["text"]
        if tool == "summarize_video":
            prompt = "仅依据字幕生成中文视频摘要，按主题列出重点，不要编造画面信息。"
            user = "字幕：\n" + text
        elif tool == "semantic_search":
            query = str(arguments.get("query", "")).strip()
            if not query:
                raise ValueError("语义搜索需要 query 参数")
            prompt = "找出与问题最相关的字幕原文句子，保留时间戳；找不到时明确说明。不要编造。"
            user = "查询：" + query + "\n字幕：\n" + text
        else:
            instruction = str(arguments.get("instruction", "")).strip()
            if not instruction:
                raise ValueError("智能剪辑需要 instruction 参数")
            prompt = ("只根据字幕时间轴选取最多 5 段与目标相关的连续片段。"
                      "仅返回 JSON：{\"segments\":[{\"start_ms\":整数,\"end_ms\":整数,\"reason\":字符串}]}。"
                      "单位毫秒，时间必须在字幕范围内，不要推测画面内容。")
            user = "目标：" + instruction + "\n字幕：\n" + text
        response = self._model().invoke([SystemMessage(content=prompt), HumanMessage(content=user)])
        current = self._require_cpp("get_media_context", {}, session_id)
        if current.get("path") != transcript["source_path"]:
            raise RuntimeError("媒体已切换，请针对当前视频重新操作。")
        result = _content(response)
        if tool != "smart_edit":
            return {"success": True, "result": result}
        segments = _json_object(result).get("segments", [])
        if not isinstance(segments, list):
            raise ValueError("模型返回的剪辑片段格式错误")
        self._require_cpp("set_edit_plan", {"source_path": transcript["source_path"], "segments": segments}, session_id)
        preview = "建议片段（点击“导出建议片段”保存）：\n" + "\n".join(
            f"{index}. {item['start_ms'] / 1000:.1f}s–{item['end_ms'] / 1000:.1f}s  {item.get('reason', '')}"
            for index, item in enumerate(segments, 1))
        return {"success": True, "result": preview, "segments": segments}

    def _transcribe(self, session_id: str) -> dict[str, Any]:
        context = self._require_cpp("get_media_context", {}, session_id)
        if not context.get("loaded"):
            raise RuntimeError("字幕转写仅支持已打开/已上传的视频。")
        source = Path(context.get("path", ""))
        if self.provider == "cloud" and not self.cloud_speech_configured():
            raise RuntimeError("请在 config/secret_key.yaml 配置 doubao_speech_api_key")
        if self.provider == "local" and not self.local_speech_configured():
            raise RuntimeError("Whisper 模型未下载或文件不完整；请运行 scripts/download_local_models.sh")
        result = self._require_cpp("transcribe_subtitle", {"provider": self.provider}, session_id)
        segments = result.get("segments", [])
        if not segments:
            raise RuntimeError("语音识别未返回带时间戳的字幕。")
        saved = self._require_cpp("save_transcript", {"source_path": str(source), "segments": segments}, session_id)
        return {"success": True, "result": f"已生成 {len(segments)} 条字幕：{saved.get('path', '')}",
                "segments": segments}

    def _build_graph(self):
        if self._graph is not None:
            return self._graph
        catalog = [tool for tool in self.tools if tool.get("name") not in INTERNAL_CPP_TOOLS] + PYTHON_TOOLS
        if self.external_tools is not None:
            external = self.external_tools()
            names = {tool['name'] for tool in external}
            catalog = [tool for tool in catalog if tool['name'] not in names] + external
        schemas = _model_tool_schemas(catalog)
        model = self._model().bind_tools(schemas)
        prompt = ("你是 AVPlayer 视频 Agent。仅调用给定工具完成媒体操作。"
                  "导出片段使用 export_video：能定位到具体内容时，先查字幕时间轴，"
                  "再传 start_ms/end_ms 导出准确区间；只有用户说“随便剪一段”时调用 export_random_clip 并传 duration_ms。"
                  "片段时长由内容决定，不要固定 3 秒；一次任务可以多次调用导出多个片段。"
                  "用户未明确指定绝对导出目录时，不要给导出工具传 output_dir。"
                  "只有工具结果 success=true 才能宣称操作完成；失败应说明原因。"
                  "最多调用 8 次工具，最终答复包含生成文件的路径。")

        def model_node(state: GraphState) -> GraphState:
            self._emit("status", self._active_request_id, self._active_session,
                       stage="planning", status="正在分析目标并选择下一步…")
            response = model.invoke([SystemMessage(content=prompt), *state["messages"]])
            return {"messages": [*state["messages"], response], "steps": state["steps"]}

        def route(state: GraphState) -> str:
            if state["steps"] >= 8 and state["messages"][-1].tool_calls:
                raise RuntimeError("工具调用次数超过 8 次，任务已终止")
            return "tools" if state["messages"][-1].tool_calls else END

        def tools_node(state: GraphState) -> GraphState:
            replies = []
            for call in state["messages"][-1].tool_calls:
                if state["steps"] >= 8:
                    result = {"success": False, "error": "工具调用次数超过上限"}
                elif call["name"] not in {tool["name"] for tool in catalog}:
                    result = {"success": False, "error": "未知工具：" + call["name"]}
                else:
                    self._emit("status", self._active_request_id, self._active_session,
                               stage="tool_started", tool=call["name"], call_id=call["id"],
                               status="正在执行：" + call["name"])
                    try:
                        result = self._perform(call["name"], call["args"], self._active_session)
                    except Exception as exc:
                        result = {"success": False, "error": str(exc)}
                    self._emit("status", self._active_request_id, self._active_session,
                               stage="tool_completed", tool=call["name"], call_id=call["id"],
                               success=bool(result.get("success", False)),
                               status=("已完成：" if result.get("success", False) else "执行失败：") + call["name"])
                replies.append(ToolMessage(content=json.dumps(result, ensure_ascii=False), tool_call_id=call["id"]))
                state["steps"] += 1
            return {"messages": [*state["messages"], *replies], "steps": state["steps"]}

        graph = StateGraph(GraphState)
        graph.add_node("model", model_node)
        graph.add_node("tools", tools_node)
        graph.add_edge(START, "model")
        graph.add_conditional_edges("model", route, {"tools": "tools", END: END})
        graph.add_edge("tools", "model")
        self._graph = graph.compile()
        return self._graph
