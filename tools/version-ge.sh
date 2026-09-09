#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    echo "usage: $0 ACTUAL MINIMUM" >&2
    exit 2
fi
actual=$1
minimum=$2
first=$(printf '%s\n%s\n' "$minimum" "$actual" | sort -V | head -n1)
[[ "$first" == "$minimum" ]]
