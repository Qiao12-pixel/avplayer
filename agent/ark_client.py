"""LangChain adapter for the optional Doubao Ark API mode."""

from __future__ import annotations

from typing import Any

from langchain_openai import ChatOpenAI


DEFAULT_ENDPOINT = "https://ark.cn-beijing.volces.com/api/v3/chat/completions"


def create_ark_model(config: dict[str, Any]) -> ChatOpenAI:
    key = config.get("doubao_api_key")
    name = config.get("doubao_model")
    if not key or not name:
        raise RuntimeError("请在 config/secret_key.yaml 配置 doubao_api_key 和 doubao_model")
    endpoint = str(config.get("doubao_chat_endpoint", DEFAULT_ENDPOINT))
    base_url = endpoint.removesuffix("/chat/completions").rstrip("/")
    return ChatOpenAI(model=str(name), api_key=str(key), base_url=base_url,
                      timeout=90, max_retries=0)
