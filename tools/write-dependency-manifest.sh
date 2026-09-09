#!/usr/bin/env bash
set -euo pipefail
root=${1:?usage: write-dependency-manifest.sh ROOTDIR}
out=${2:-$root/etc/sshfs-win-dependencies.txt}
origins=${3:-$root/etc/runtime-origins.tsv}
mkdir -p "$(dirname "$out")"
[[ -f "$origins" ]] || { echo "FAIL runtime origin map missing: $origins" >&2; exit 40; }

required_version() {
    local name=$1
    shift
    local value
    if ! value=$("$@" 2>&1); then
        echo "FAIL cannot query required tool version: $name" >&2
        printf '%s\n' "$value" >&2
        exit 42
    fi
    value=$(printf '%s\n' "$value" | tr -d '\r' | head -n1)
    [[ -n "$value" ]] || {
        echo "FAIL required tool version is empty: $name" >&2
        exit 43
    }
    printf '%s\n' "$value"
}

{
    echo '# SSHFS-Win packaged dependency manifest'
    echo "generated_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "cygwin=$(required_version cygwin uname -r)"
    echo "meson=$(required_version meson meson --version)"
    echo "ninja=$(required_version ninja ninja --version)"
    echo
    echo '[files]'
    find "$root/bin" -maxdepth 1 -type f -print0 | sort -z | while IFS= read -r -d '' f; do
        base=$(basename "$f")
        hash=$(sha256sum "$f" | awk '{print $1}')
        origin=$(awk -F '\t' -v b="$base" '$1 == b {print $2; exit}' "$origins")
        [[ -n "$origin" ]] || {
            echo "FAIL no provenance for packaged runtime: $base" >&2
            exit 41
        }
        printf '%s\t%s\t%s\n' "$base" "$hash" "$origin"
    done
} > "$out"
