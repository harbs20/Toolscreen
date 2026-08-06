#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
RUNTIME_DYLIB="$REPO_ROOT/out/build-macos-portable/bin/Debug/libToolscreenMacRuntime.dylib"
LOG_PATH="${TOOLSCREEN_MACOS_LOG_PATH:-$HOME/Library/Logs/Toolscreen/prism-runtime.log}"
SETTINGS_PATH="${TOOLSCREEN_MACOS_SETTINGS_PATH:-$HOME/Library/Application Support/Toolscreen/macos-runtime.properties}"

if [ ! -f "$RUNTIME_DYLIB" ]; then
    printf '%s\n' "Toolscreen macOS runtime is missing: $RUNTIME_DYLIB" >&2
    printf '%s\n' "Build it first with: /opt/homebrew/bin/cmake --build --preset macos-portable-debug --parallel" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG_PATH")"
mkdir -p "$(dirname "$SETTINGS_PATH")"

if [ ! -f "$SETTINGS_PATH" ]; then
    {
        printf '%s\n' 'overlay.enabled=true'
        printf '%s\n' 'overlay.mode=thin'
        printf '%s\n' 'overlay.scale=1.00'
    } > "$SETTINGS_PATH"
fi

{
    printf '%s pid=%s prism-wrapper starting\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$$"
    printf '%s pid=%s runtime=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$$" "$RUNTIME_DYLIB"
    printf '%s pid=%s settings=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$$" "$SETTINGS_PATH"
} >> "$LOG_PATH"

export DYLD_INSERT_LIBRARIES="$RUNTIME_DYLIB${DYLD_INSERT_LIBRARIES:+:$DYLD_INSERT_LIBRARIES}"
export TOOLSCREEN_MACOS_AUTO_INSTALL_OPENGL_HOOK=1
export TOOLSCREEN_MACOS_HOTKEYS="${TOOLSCREEN_MACOS_HOTKEYS:-1}"
export TOOLSCREEN_MACOS_NINJABRAIN="${TOOLSCREEN_MACOS_NINJABRAIN:-1}"
export TOOLSCREEN_MACOS_NINJABRAIN_API_BASE_URL="${TOOLSCREEN_MACOS_NINJABRAIN_API_BASE_URL:-http://127.0.0.1:52533}"
export TOOLSCREEN_MACOS_OVERLAY="${TOOLSCREEN_MACOS_OVERLAY:-1}"
export TOOLSCREEN_MACOS_DEBUG_OVERLAY="${TOOLSCREEN_MACOS_DEBUG_OVERLAY:-1}"
export TOOLSCREEN_MACOS_LOG_PATH="$LOG_PATH"
export TOOLSCREEN_MACOS_SETTINGS_PATH="$SETTINGS_PATH"

exec "$@"
