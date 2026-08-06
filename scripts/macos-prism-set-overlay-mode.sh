#!/bin/sh
set -eu

SETTINGS_PATH="${TOOLSCREEN_MACOS_SETTINGS_PATH:-$HOME/Library/Application Support/Toolscreen/macos-runtime.properties}"

usage() {
    printf '%s\n' "Usage: $0 <thin|wide|eyezoom|fullscreen|marker|off>" >&2
}

if [ "$#" -ne 1 ]; then
    usage
    exit 2
fi

mode=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
scale=1.00
if [ -f "$SETTINGS_PATH" ]; then
    existing_scale=$(awk -F= 'tolower($1) == "overlay.scale" { gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit }' "$SETTINGS_PATH")
    if [ -n "$existing_scale" ]; then
        scale="$existing_scale"
    fi
fi

case "$mode" in
    thin|wide|eyezoom|fullscreen|marker)
        enabled=true
        ;;
    off)
        enabled=false
        mode=thin
        ;;
    *)
        usage
        exit 2
        ;;
esac

mkdir -p "$(dirname "$SETTINGS_PATH")"
{
    printf 'overlay.enabled=%s\n' "$enabled"
    printf 'overlay.mode=%s\n' "$mode"
    printf 'overlay.scale=%s\n' "$scale"
} > "$SETTINGS_PATH"

printf 'ToolScreen macOS overlay: enabled=%s mode=%s scale=%s\n' "$enabled" "$mode" "$scale"
printf 'Settings: %s\n' "$SETTINGS_PATH"
