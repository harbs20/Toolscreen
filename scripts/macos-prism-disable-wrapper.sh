#!/bin/sh
set -eu

INSTANCE_ROOT="${PRISM_INSTANCE_ROOT:-$HOME/Library/Application Support/PrismLauncher/instances}"

usage() {
    printf '%s\n' "Usage: $0 <Prism instance folder or display name>" >&2
    printf '%s\n' "Example: $0 mcsr" >&2
}

lower() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

read_instance_name() {
    awk -F= '$1 == "name" { print substr($0, index($0, "=") + 1); exit }' "$1"
}

find_instance_dir() {
    wanted=$1

    if [ -d "$INSTANCE_ROOT/$wanted" ]; then
        printf '%s\n' "$INSTANCE_ROOT/$wanted"
        return 0
    fi

    wanted_lower=$(lower "$wanted")
    for cfg in "$INSTANCE_ROOT"/*/instance.cfg; do
        [ -f "$cfg" ] || continue
        name=$(read_instance_name "$cfg")
        if [ "$(lower "$name")" = "$wanted_lower" ]; then
            dirname "$cfg"
            return 0
        fi
    done

    return 1
}

if [ "$#" -ne 1 ]; then
    usage
    exit 2
fi

INSTANCE_DIR=$(find_instance_dir "$1") || {
    printf '%s\n' "Prism instance not found: $1" >&2
    exit 1
}

CFG="$INSTANCE_DIR/instance.cfg"
BACKUP="$CFG.toolscreen-backup"

if [ -f "$BACKUP" ]; then
    cp "$BACKUP" "$CFG"
    printf '%s\n' "Restored Prism instance config from: $BACKUP"
    exit 0
fi

if [ ! -f "$CFG" ]; then
    printf '%s\n' "Prism instance config not found: $CFG" >&2
    exit 1
fi

TMP="$CFG.toolscreen-tmp"
awk '
BEGIN {
    in_general = 0
}

/^\[/ {
    in_general = ($0 == "[General]")
    print
    next
}

in_general && /^OverrideCommands=/ {
    print "OverrideCommands=false"
    next
}

in_general && /^WrapperCommand=/ {
    print "WrapperCommand="
    next
}

{
    print
}
' "$CFG" > "$TMP"

mv "$TMP" "$CFG"
printf '%s\n' "Disabled Toolscreen wrapper for: $INSTANCE_DIR"
