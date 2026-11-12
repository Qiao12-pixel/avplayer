# Tests

Test layers after the deployment-unit split:

| Path | Kind | Command |
| --- | --- | --- |
| `services/agent/tests/` | Python unit tests (runtime/graph, transcript cache, MySQL persistence, clip export) | `.venv/bin/python -m unittest discover -s services/agent/tests -v` |
| `tests/grpc_smoke_client.py` | in-process gRPC upload + task smoke test | `.venv/bin/python tests/grpc_smoke_client.py` |
| `tests/manual_transcribe_check.py` | end-to-end transcription against a running agent service | `.venv/bin/python tests/manual_transcribe_check.py resource/input.mp4` |
| `services/media/CMakeLists.txt` (`streaming_smoke_client`) | WebRTC receive smoke client | `cmake --build cmake-build-debug --target streaming_smoke_client` |

Install the package (or set `PYTHONPATH=services/agent`) before running the
Python tests:

```sh
.venv/bin/python -m pip install -e 'services/agent[dev]'
```

`tests/grpc_smoke_client.py` starts its own server on `127.0.0.1:50061`;
`manual_transcribe_check.py` expects the agent service from `scripts/dev.sh`
or Compose to be running.

The cache/database tests use in-memory fakes, so they pass with no Redis or
MySQL running. To exercise the real services, set `AVPLAYER_REDIS_URL` and
`AVPLAYER_MYSQL_DSN` and use `tests/manual_transcribe_check.py`.
