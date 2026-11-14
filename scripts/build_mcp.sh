#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MCP_ROOT="${AVPLAYER_MCP_ROOT:-$ROOT/thirdParty/MCP}"
MCP_BUILD="${AVPLAYER_MCP_BUILD_DIR:-$ROOT/build/mcp}"
# Install dependencies once with Conan, or pass preinstalled CMake package paths.
if [[ "${1:-}" == "--install-deps" ]]; then
  CONAN="${CONAN:-conan}"
  "$CONAN" install "$MCP_ROOT" -of "$MCP_BUILD" -s build_type=Debug --build=missing
  shift
fi
cmake -S "$MCP_ROOT" -B "$MCP_BUILD" -DCMAKE_BUILD_TYPE=Debug -DMCP_ENABLE_AVPLAYER=ON "$@"
cmake --build "$MCP_BUILD" --target MCP_Server -j "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
