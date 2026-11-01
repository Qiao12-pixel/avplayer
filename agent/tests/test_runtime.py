from __future__ import annotations

import io
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from langchain_core.messages import AIMessage, ToolMessage

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import AgentRuntime, _model_tool_schemas  # noqa: E402


def messages(output: io.StringIO) -> list[dict]:
    return [json.loads(line) for line in output.getvalue().splitlines()]


def wait_for(output: io.StringIO, kind: str) -> dict:
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        matches = [item for item in messages(output) if item["type"] == kind]
        if matches:
            return matches[-1]
        time.sleep(0.01)
    raise AssertionError(f"missing {kind}: {messages(output)}")


class FakeModel:
    def bind_tools(self, schemas):
        assert any(item["function"]["name"] == "get_media_context" for item in schemas)
        return self

    def invoke(self, history):
        if isinstance(history[-1], ToolMessage):
            return AIMessage(content="媒体已加载。")
        return AIMessage(content="", tool_calls=[{
            "name": "get_media_context", "args": {}, "id": "model-call-1"
        }])


class RuntimeTests(unittest.TestCase):
    def test_model_catalog_normalizes_empty_objects_and_hides_internal_tools(self):
        tools = [
            {"name": "get_media_context", "description": "读取媒体", "input_schema": {"type": "object"}},
            {"name": "save_transcript", "description": "保存字幕", "input_schema": {"type": "object"}},
            {"name": "export_random_clip", "description": "随机导出", "input_schema": {
                "type": "object", "properties": {"duration_ms": {"type": "integer"}},
                "required": ["duration_ms"],
            }},
        ]
        schemas = _model_tool_schemas(tools)
        self.assertEqual([item["function"]["name"] for item in schemas],
                         ["get_media_context", "export_random_clip"])
        self.assertEqual(schemas[0]["function"]["parameters"]["properties"], {})
        self.assertEqual(schemas[0]["function"]["parameters"]["required"], [])
        self.assertEqual(schemas[1]["function"]["parameters"]["required"], ["duration_ms"])

    def test_model_catalog_rejects_non_object_properties(self):
        with self.assertRaisesRegex(ValueError, "properties 必须是对象"):
            _model_tool_schemas([{"name": "broken", "input_schema": {
                "type": "object", "properties": []
            }}])

    def test_invalid_config_does_not_crash_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "resource").mkdir()
            (root / "resource" / "local-model.yaml").write_text("model: [\n", encoding="utf-8")
            with patch.dict(os.environ, {"AVPLAYER_PROJECT_ROOT": directory}):
                runtime = AgentRuntime(io.StringIO(), model=FakeModel())
            self.assertIn("YAML 配置格式错误", runtime.provider_status()["config_status"])

    def test_yaml_error_never_echoes_credential(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "config").mkdir()
            (root / "config" / "secret_key.yaml").write_text(
                "doubao_api_key: private-secret-value\nbroken: [\n", encoding="utf-8"
            )
            with patch.dict(os.environ, {"AVPLAYER_PROJECT_ROOT": directory,
                                             "AVPLAYER_AI_PROVIDER": "cloud"}):
                runtime = AgentRuntime(io.StringIO(), model=FakeModel())
            status = runtime.provider_status()["config_status"]
            self.assertRegex(status, r"第 \d+ 行")
            self.assertNotIn("private-secret-value", status)

    def test_whisper_requires_weights_and_config(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / "resource" / "whisper-small" / "snapshot"
            model.mkdir(parents=True)
            (root / "resource" / "local-model.yaml").write_text(
                "model: qwen3:4b\nwhisper_model_path: resource/whisper-small\n", encoding="utf-8"
            )
            with patch.dict(os.environ, {"AVPLAYER_PROJECT_ROOT": directory}):
                runtime = AgentRuntime(io.StringIO(), model=FakeModel())
            (model / "config.json").write_text("{}", encoding="utf-8")
            self.assertFalse(runtime.local_speech_configured())
            (model / "model.bin").write_bytes(b"test")
            self.assertTrue(runtime.local_speech_configured())

    def test_missing_whisper_fails_before_audio_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "video.mp4"
            source.write_bytes(b"test")
            output = io.StringIO()
            runtime = AgentRuntime(output, model=FakeModel())
            calls = []
            def fake_cpp(tool, arguments, session_id):
                calls.append(tool)
                return {"success": True, "loaded": True, "path": str(source)}
            with patch.object(runtime, "_require_cpp", side_effect=fake_cpp), \
                 patch.object(runtime, "local_speech_configured", return_value=False):
                with self.assertRaisesRegex(RuntimeError, "Whisper 模型未下载"):
                    runtime._transcribe("desktop")
            self.assertEqual(calls, ["get_media_context"])

    def test_provider_switch_reports_selected_path(self):
        output = io.StringIO()
        runtime = AgentRuntime(output, model=FakeModel())
        with patch.object(runtime, "local_model_available", return_value=False), \
             patch.object(runtime, "local_speech_configured", return_value=False):
            runtime.handle({"type": "provider", "request_id": "switch", "session_id": "runtime",
                            "payload": {"provider": "cloud"}})
        response = messages(output)[-1]
        self.assertEqual(response["type"], "provider_status")
        self.assertEqual(response["payload"]["provider"], "cloud")
        self.assertIsNone(runtime.model)

    def test_provider_switch_while_busy_reverts(self):
        output = io.StringIO()
        runtime = AgentRuntime(output, model=FakeModel())
        class BusyThread:
            def is_alive(self): return True
        runtime._worker = BusyThread()
        with patch.object(runtime, "provider_status", return_value={
            "provider": "local", "model_configured": True,
            "speech_configured": True, "config_status": "本地模型就绪。"
        }):
            runtime.handle({"type": "provider", "request_id": "switch", "session_id": "runtime",
                            "payload": {"provider": "cloud"}})
        self.assertEqual(runtime.provider, "local")
        response = messages(output)[-1]
        self.assertEqual(response["type"], "provider_status")
        self.assertEqual(response["payload"]["provider"], "local")
        self.assertIn("无法切换", response["payload"]["config_status"])

    def test_graph_reports_named_tool_lifecycle(self):
        output = io.StringIO()
        runtime = AgentRuntime(output, model=FakeModel())
        runtime.set_tools([{"name": "get_media_context", "description": "读取媒体状态",
                            "input_schema": {"type": "object", "properties": {}}}])
        runtime.handle({"type": "goal", "request_id": "goal-1", "session_id": "desktop",
                        "payload": {"goal": "检查媒体状态"}})
        call = wait_for(output, "tool_call")
        self.assertEqual(call["tool"], "get_media_context")
        runtime.handle({"type": "tool_result", "request_id": call["request_id"],
                        "session_id": "desktop", "tool": "get_media_context",
                        "payload": {"success": True, "loaded": True}})
        self.assertEqual(wait_for(output, "final")["payload"]["text"], "媒体已加载。")
        stages = [item for item in messages(output) if item["type"] == "status"]
        started = next(item for item in stages if item["payload"].get("stage") == "tool_started")
        completed = next(item for item in stages if item["payload"].get("stage") == "tool_completed")
        self.assertEqual(started["tool"], "get_media_context")
        self.assertEqual(completed["tool"], "get_media_context")
        self.assertEqual(started["payload"]["call_id"], completed["payload"]["call_id"])


if __name__ == "__main__":
    unittest.main()
