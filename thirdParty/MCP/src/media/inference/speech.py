"""Speech inference implementation, executed only inside a Worker child process."""
from pathlib import Path
from typing import Any
import json
import uuid
import yaml
import os
def project_root(): return Path(os.environ.get("AVPLAYER_PROJECT_ROOT", ".")).resolve()

class SpeechEngine:
    def __init__(self):
        self.project_root = project_root()
        def load(path):
            if not path.is_file(): return {}
            try:
                value = yaml.safe_load(path.read_text(encoding="utf-8"))
            except yaml.YAMLError as error:
                raise ValueError("invalid YAML configuration: " + path.name) from None
            return value if isinstance(value, dict) else {}
        self.local_config = load(self.project_root / "resource/local-model.yaml")
        self.config = load(self.project_root / "config/secret_key.yaml")

    def _transcribe_local(self, audio_path: Path) -> list[dict[str, Any]]:
        model_path = Path(str(self.local_config.get("whisper_model_path", "")))
        if not model_path.is_absolute(): model_path = self.project_root / model_path
        from faster_whisper import WhisperModel
        model_config = next((config for config in model_path.rglob("config.json")
                             if (config.parent / "model.bin").is_file()), None) if model_path.is_dir() else None
        if model_config is not None:
            whisper = WhisperModel(str(model_config.parent), device="auto", compute_type="int8")
        else:
            raise RuntimeError("Whisper 模型未下载；请运行 scripts/download_local_models.sh")
        chunks, _ = whisper.transcribe(str(audio_path), vad_filter=True)
        return [{"start_ms": int(chunk.start * 1000), "end_ms": int(chunk.end * 1000),
                 "text": chunk.text.strip()} for chunk in chunks if chunk.text.strip()]

    def _transcribe_cloud(self, audio_path: Path) -> list[dict[str, Any]]:
        import base64
        import urllib.error
        import urllib.request
        body = {"user": {"uid": "avplayer"},
                "audio": {"data": base64.b64encode(audio_path.read_bytes()).decode("ascii")},
                "request": {"model_name": "bigmodel"}}
        request = urllib.request.Request(
            "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash",
            data=json.dumps(body).encode("utf-8"), method="POST",
            headers={"Content-Type": "application/json",
                     "X-Api-Key": str(self.config["doubao_speech_api_key"]),
                     "X-Api-Resource-Id": "volc.bigasr.auc_turbo",
                     "X-Api-Request-Id": str(uuid.uuid4()), "X-Api-Sequence": "-1"})
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                status = response.headers.get("X-Api-Status-Code", "")
                result = json.load(response)
        except urllib.error.HTTPError as exc:
            raise RuntimeError(f"豆包语音请求失败（HTTP {exc.code}）：{exc.read().decode('utf-8', 'replace')[-400:]}") from exc
        if status != "20000000":
            raise RuntimeError("豆包语音识别失败：" + str(result.get("message", status)))
        return [{"start_ms": item["start_time"], "end_ms": item["end_time"], "text": item["text"]}
                for item in result.get("result", {}).get("utterances", []) if item.get("text")]


if __name__ == "__main__":
    import sys
    audio, output = map(Path, sys.argv[1:3])
    provider = sys.argv[3]
    try:
        engine = SpeechEngine()
        if provider not in ("local", "cloud"):
            raise ValueError("invalid speech provider")
        segments = engine._transcribe_local(audio) if provider == "local" else engine._transcribe_cloud(audio)
        value = {"success": bool(segments), "segments": segments, "error": "" if segments else "empty speech result"}
    except Exception as error:
        value = {"success": False, "error": str(error)}
    output.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
