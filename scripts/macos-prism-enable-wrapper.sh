#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTANCE_ROOT="${PRISM_INSTANCE_ROOT:-$HOME/Library/Application Support/PrismLauncher/instances}"
WRAPPER="$SCRIPT_DIR/macos-prism-wrapper.sh"

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

if [ ! -d "$INSTANCE_ROOT" ]; then
    printf '%s\n' "Prism instance directory not found: $INSTANCE_ROOT" >&2
    exit 1
fi

INSTANCE_DIR=$(find_instance_dir "$1") || {
    printf '%s\n' "Prism instance not found: $1" >&2
    exit 1
}

CFG="$INSTANCE_DIR/instance.cfg"
BACKUP="$CFG.toolscreen-backup"
TMP="$CFG.toolscreen-tmp"

if [ ! -f "$CFG" ]; then
    printf '%s\n' "Prism instance config not found: $CFG" >&2
    exit 1
fi

if [ ! -f "$BACKUP" ]; then
    cp "$CFG" "$BACKUP"
fi

awk -v wrapper="$WRAPPER" '
function emit_missing_general_keys() {
    if (!seen_override_commands) {
        print "OverrideCommands=true"
    }
    if (!seen_wrapper_command) {
        print "WrapperCommand=" wrapper
    }
}

BEGIN {
    in_general = 0
    seen_general = 0
    seen_override_commands = 0
    seen_wrapper_command = 0
}

/^\[/ {
    if (in_general) {
        emit_missing_general_keys()
    }
    in_general = ($0 == "[General]")
    if (in_general) {
        seen_general = 1
    }
    print
    next
}

in_general && /^OverrideCommands=/ {
    print "OverrideCommands=true"
    seen_override_commands = 1
    next
}

in_general && /^WrapperCommand=/ {
    print "WrapperCommand=" wrapper
    seen_wrapper_command = 1
    next
}

{
    print
}

END {
    if (in_general) {
        emit_missing_general_keys()
    } else if (!seen_general) {
        print "[General]"
        print "OverrideCommands=true"
        print "WrapperCommand=" wrapper
    }
}
' "$CFG" > "$TMP"

mv "$TMP" "$CFG"
chmod +x "$WRAPPER"

printf '%s\n' "Enabled Toolscreen wrapper for: $INSTANCE_DIR"
printf '%s\n' "Backup: $BACKUP"
printf '%s\n' "Wrapper: $WRAPPER"
