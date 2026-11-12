# AVPlayer Media Service

C++ deployment unit that decodes media from the shared storage root, encodes
H.264 video + Opus audio and pushes them over WebRTC. It links `libs/media-core` and `proto`
directly; it does not link or build the Qt desktop application.

```text
services/media/
├── CMakeLists.txt            # standalone project + media_server target
└── src/
    ├── main_media_server.cpp # process entry, env-based bind/storage
    ├── MediaServer.{h,cpp}   # StreamingService gRPC implementation
    ├── MediaSession.{h,cpp}  # per-session decode/sync/WebRTC pipeline
    ├── WebRtcFrameSink.*     # H.264 encoding and RTP packetization
    ├── WebRtcAudioSink.*     # PCM → 48 kHz Opus encoding and RTP packetization
    └── streaming_smoke_client.cpp (optional receive-only smoke client)
```

## Build

Standalone (only media-core/proto are configured):

```sh
cmake -S services/media -B build/media -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/media --target media_server -j
```

On macOS the repository's Conan generator only publishes Debug FFmpeg
libraries, so standalone builds must use `Debug`; the Linux container uses
pkg-config and builds in `Release`.

From the repository root, the same target is part of the normal build:

```sh
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target media_server
```

On macOS this uses the Conan/Homebrew dependency setup from the root
`CMakeLists.txt`; on Debian/Ubuntu it falls back to pkg-config for FFmpeg.

## Run

```sh
AVPLAYER_MEDIA_SERVER_ADDRESS=0.0.0.0:50062 \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
AVPLAYER_ICE_SERVERS=stun:stun.l.google.com:19302 \
./cmake-build-debug/services/media/media_server
```

| Variable | Default | Purpose |
| --- | --- | --- |
| `AVPLAYER_MEDIA_SERVER_ADDRESS` | `0.0.0.0:50062` | gRPC Signaling/Control bind |
| `AVPLAYER_STORAGE_ROOT` | `<project>/storage` | uploaded media root |
| `AVPLAYER_ICE_SERVERS` | unset | comma-separated STUN/TURN URLs for libdatachannel |

Opus (`libopus` / `Opus::opus`) is required for the audio track. `Control`
supports `play`, `pause`, `seek`, `status`, `record_start` and `record_stop`;
`ControlAck` returns duration/position/playing/recording and the recording
output path. Server-side recordings are written next to the uploaded media as
`recording-<millis>.mp4`.

The service resolves `storage/<session_id>/<media_id>/original.*`, which is
written by `services/agent`'s `MediaService.UploadMedia`.

## Smoke client

```sh
cmake --build cmake-build-debug --target streaming_smoke_client
./cmake-build-debug/services/media/streaming_smoke_client localhost:50062 <session_id> <media_id>
```
