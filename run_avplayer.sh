#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${1:-"$PROJECT_ROOT/cmake-build-debug"}
shift || true

QT_ROOT=/opt/homebrew/Cellar/qtbase/6.10.2
QT_MM_ROOT=/opt/homebrew/Cellar/qtmultimedia/6.10.2
QT_DECL_ROOT=/opt/homebrew/Cellar/qtdeclarative/6.10.2

export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_ROOT/share/qt/plugins/platforms"
export DYLD_FRAMEWORK_PATH="$QT_ROOT/lib:$QT_MM_ROOT/lib:$QT_DECL_ROOT/lib${DYLD_FRAMEWORK_PATH:+:$DYLD_FRAMEWORK_PATH}"

exec "$BUILD_DIR/AVPlayer" "$@"
