#!/usr/bin/env bash
set -euo pipefail

SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SRC=${1:-}
DST=${2:-}

if [[ -z "$SRC" || -z "$DST" || ! -d "$SRC" ]]; then
    echo "usage: $0 /path/to/pristine-sshfs /path/to/staged-sshfs" >&2
    exit 2
fi

SRC=$(cd -- "$SRC" && pwd)
case "$DST" in
    /*) ;;
    *) DST="$(pwd)/$DST" ;;
esac

# Verify the authority before copying, then verify the staged bytes/modes again.
# Plain recursive copy deliberately avoids archive/ACL preservation: Windows
# directory ownership metadata is neither part of Git tree identity nor
# reliably writable through Cygwin on hosted or locked-down NTFS volumes.
"$SELF_DIR/verify-sshfs-source.sh" --source-only "$SRC" >/dev/null
rm -rf -- "$DST"
mkdir -p -- "$(dirname -- "$DST")"
cp -R -- "$SRC" "$DST"
"$SELF_DIR/verify-sshfs-source.sh" --source-only "$DST" >/dev/null

printf 'staged pristine SSHFS source: %s -> %s\n' "$SRC" "$DST"
