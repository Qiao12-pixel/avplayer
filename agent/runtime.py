"""LangGraph orchestration; all model and speech requests stay in Python."""

from __future__ import annotations

import base64
import json
import os
import threading
import urllib.error
import urllib.request
import uuid
from pathlib import Path
from typing import Any, Callable, TypedDict

import yaml
from langchain_core.messages import AIMessage, HumanMessage, SystemMessage, ToolMessage
from langchain_openai import ChatOpenAI
from langgraph.graph import END, START, StateGraph

from protocol import emit, message


TASK_NAMES = {
    "transcribe_subtitle": "字幕转写",
    "summarize_video": "视频摘要",
    "semantic_search": "语义搜索",
    "smart_edit": "智能剪辑",
}

PYTHON_TOOLS = [
    {"name": "transcribe_subtitle", "description": "为当前本地视频转写音频并生成带时间戳的字幕。", "input_schema": {"type": "object", "properties": {}}},
    {"name": "summarize_video", "description": "仅依据当前视频的字幕生成摘要；需要已有字幕。", "input_schema": {"type": "object", "properties": {}}},
    {"name": "semantic_search", "description": "根据当前视频字幕查找语义相关内容。", "input_schema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "smart_edit", "description": "根据带时间戳的字幕生成剪辑建议。完成后可调用 export_clip。", "input_schema": {"type": "object", "properties": {"instruction": {"type": "string"}}, "required": ["instruction"]}},
]


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
    def __init__(self, output: Any, model: Any | None = None) -> None:
        self.output = output
        self._write_lock = threading.Lock()
        self._condition = threading.Condition()
        self._pending: dict[str, dict[str, Any]] = {}
        self._worker: threading.Thread | None = None
        self._worker_lock = threading.Lock()
        self.tools: list[dict[str, Any]] = []
        root = Path(os.environ.get("AVPLAYER_PROJECT_ROOT", Path.cwd()))
        config_path = root / "config" / "secret_key.yaml"
        config = yaml.safe_load(config_path.read_text(encoding="utf-8")) if config_path.is_file() else {}
        self.config = config if isinstance(config, dict) else {}
        self.model = model
        self._graph = None
        self._active_session = ""

    def _emit(self, kind: str, request_id: str, session_id: str, **fields: Any) -> None:
        with self._write_lock:
            emit(message(kind, request_id, session_id, **fields), self.output)

    def _model(self) -> Any:
        if self.model is None:
            key = self.config.get("doubao_api_key")
            name = self.config.get("doubao_model")
            if not key or not name:
                raise RuntimeError("请在 config/secret_key.yaml 配置 doubao_api_key 和 doubao_model")
            endpoint = self.config.get("doubao_chat_endpoint", "https://ark.cn-beijing.volces.com/api/v3/chat/completions")
            base_url = endpoint.removesuffix("/chat/completions").rstrip("/")
            self.model = ChatOpenAI(model=name, api_key=key, base_url=base_url,
                                    timeout=90, max_retries=0)
        return self.model

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
        elif kind == "tool_result":
            with self._condition:
                if request_id in self._pending:
                    self._pending[request_id] = data.get("payload", {})
                    self._condition.notify_all()
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
            if kind == "task":
                task = data.get("payload", {}).get("task", "")
                if task not in TASK_NAMES:
                    raise ValueError("未知 AI 任务：" + task)
                result = self._perform(task, data.get("arguments", {}), session_id)
                self._emit("task_result", request_id, session_id, task=task, **result)
            else:
                goal = str(data.get("payload", {}).get("goal", "")).strip()
                if not goal:
                    raise ValueError("任务目标不能为空")
                self._active_session = session_id
                self._emit("status", request_id, session_id, status="正在规划任务")
                state = self._build_graph().invoke({"messages": [HumanMessage(content=goal)], "steps": 0},
                                                   config={"recursion_limit": 25})
                self._emit("final", request_id, session_id, text=_content(state["messages"][-1]))
        except Exception as exc:
            self._emit("task_result" if kind == "task" else "error", request_id, session_id,
                       error=str(exc), **({"task": data.get("payload", {}).get("task", "")} if kind == "task" else {}))
        finally:
            with self._worker_lock:
                if self._worker is threading.current_thread():
                    self._worker = None

    def _call_cpp(self, tool: str, arguments: dict[str, Any], session_id: str) -> dict[str, Any]:
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
        source = Path(context.get("path", ""))
        if not context.get("loaded") or not source.is_file():
            raise RuntimeError("字幕转写仅支持已打开的本地视频。")
        key = self.config.get("doubao_speech_api_key")
        if not key:
            raise RuntimeError("请配置 doubao_speech_api_key")
        audio = self._require_cpp("extract_audio", {"source_path": str(source)}, session_id)
        audio_path = Path(audio["path"])
        try:
            body = {"user": {"uid": "avplayer"},
                    "audio": {"data": base64.b64encode(audio_path.read_bytes()).decode("ascii")},
                    "request": {"model_name": "bigmodel"}}
        finally:
            audio_path.unlink(missing_ok=True)
        request = urllib.request.Request(
            "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash",
            data=json.dumps(body).encode("utf-8"), method="POST",
            headers={"Content-Type": "application/json", "X-Api-Key": key,
                     "X-Api-Resource-Id": "volc.bigasr.auc_turbo",
                     "X-Api-Request-Id": str(uuid.uuid4()), "X-Api-Sequence": "-1"})
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                status = response.headers.get("X-Api-Status-Code", "")
                result = json.load(response)
        except urllib.error.HTTPError as exc:
            raise RuntimeError(f"豆包语音请求失败（HTTP {exc.code}）：{exc.read().decode('utf-8', 'replace')[-400:]}") from exc
        if status != "20000000":
            raise RuntimeError("豆包语音识别失败：" + result.get("message", status))
        utterances = result.get("result", {}).get("utterances", [])
        segments = [{"start_ms": item["start_time"], "end_ms": item["end_time"], "text": item["text"]}
                    for item in utterances if item.get("text")]
        if not segments:
            raise RuntimeError("豆包识别完成，但没有返回带时间戳的字幕。")
        saved = self._require_cpp("save_transcript", {"source_path": str(source), "segments": segments}, session_id)
        return {"success": True, "result": f"已生成 {len(segments)} 条字幕：{saved.get('path', '')}"}

    def _build_graph(self):
        if self._graph is not None:
            return self._graph
        catalog = self.tools + PYTHON_TOOLS
        schemas = [{"type": "function", "function": {"name": item["name"],
                    "description": item["description"], "parameters": item["input_schema"]}}
                   for item in catalog]
        model = self._model().bind_tools(schemas)
        prompt = ("你是 AVPlayer 视频 Agent。仅调用给定工具完成媒体操作。"
                  "随机导出片段使用 export_random_clip，duration_ms 是毫秒。"
                  "只有工具结果 success=true 才能宣称操作完成；失败应说明原因。"
                  "最多调用 8 次工具，最终答复包含生成文件的路径。")

        def model_node(state: GraphState) -> GraphState:
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
                    try:
                        result = self._perform(call["name"], call["args"], self._active_session)
                    except Exception as exc:
                        result = {"success": False, "error": str(exc)}
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
