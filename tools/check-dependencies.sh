#!/usr/bin/env bash
set -euo pipefail
SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
VER_GE="$SELF_DIR/tools/version-ge.sh"
MINIMUMS="$SELF_DIR/deps/MINIMUMS.tsv"
mode=${1:---build}
fail=0

minimum_for() {
    local component=$1
    local value
    value=$(awk -F '\t' -v component="$component" '
        NR == 1 { next }
        $1 == component { print $2; found=1; exit }
        END { if (!found) exit 4 }
    ' "$MINIMUMS") || {
        echo "missing dependency minimum for $component in $MINIMUMS" >&2
        exit 3
    }
    printf '%s\n' "$value"
}

check_cmd() {
    local name=$1 min=$2
    shift 2
    local raw actual
    if ! raw=$("$@" 2>&1); then
        printf 'FAIL %-12s command=%q error=%s\n' "$name" "$1" "$raw" >&2
        fail=1
        return
    fi
    actual=$(printf '%s\n' "$raw" | awk '
        match($0, /[0-9]+([.][0-9]+)+/) {
            print substr($0, RSTART, RLENGTH)
            exit
        }')
    if [[ -z "$actual" ]] || ! "$VER_GE" "$actual" "$min"; then
        printf 'FAIL %-12s actual=%s minimum=%s\n' "$name" "${actual:-unknown}" "$min" >&2
        fail=1
    else
        printf 'PASS %-12s actual=%s minimum=%s\n' "$name" "$actual" "$min"
    fi
}

check_cmd cygwin "$(minimum_for cygwin)" uname -r
check_cmd dotnet 6.0 dotnet --version
if [[ "$mode" != "--bootstrap" ]]; then
    check_cmd glib-2.0 "$(minimum_for glib-2.0)" pkg-config --modversion glib-2.0
fi
check_cmd fuse3 3.2 pkg-config --modversion fuse3
check_cmd meson "$(minimum_for meson)" meson --version
check_cmd ninja "$(minimum_for ninja)" ninja --version

if [[ "$mode" == "--vendor-ssh" ]]; then
    command -v perl >/dev/null || { echo 'FAIL perl missing' >&2; fail=1; }
    command -v make >/dev/null || { echo 'FAIL make missing' >&2; fail=1; }
    command -v gcc >/dev/null || { echo 'FAIL gcc missing' >&2; fail=1; }
    command -v git >/dev/null || { echo 'FAIL git missing' >&2; fail=1; }
fi

exit "$fail"
