#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

# Released/checkpoint source must not depend on nested repository metadata.
[[ ! -e "$ROOT/sshfs/.git" ]] || { echo 'bundled SSHFS unexpectedly contains nested .git metadata' >&2; exit 1; }
"$ROOT/tools/verify-sshfs-source.sh" --source-only "$ROOT/sshfs" >/dev/null

makefile=$(cat "$ROOT/Makefile")
[[ "$makefile" != *'git -c core.autocrlf=false clone $(PrjDir)/sshfs'* ]] || {
    echo 'Makefile still requires bundled SSHFS to be a git repository' >&2; exit 1;
}
[[ "$makefile" == *'cp -a "$(PrjDir)/sshfs" "$(SrcDir)/sshfs"'* ]] || {
    echo 'Makefile does not stage bundled plain SSHFS source' >&2; exit 1;
}
[[ "$makefile" == *'verify-sshfs-source.sh --source-only "$(SrcDir)/sshfs"'* ]] || {
    echo 'Makefile does not verify staged SSHFS source identity' >&2; exit 1;
}
[[ "$makefile" != *'git clean -dffx'* ]] || {
    echo 'make clean still requires top-level git metadata' >&2; exit 1;
}
[[ "$makefile" == *'rm -rf -- "$(PrjDir)/.build"'* ]] || {
    echo 'make clean is not source-archive-safe' >&2; exit 1;
}

# File mode is part of the pinned Git tree and must survive packaging.
[[ -x "$ROOT/sshfs/test/test_sshfs.py" ]] || {
    echo 'SSHFS executable mode was lost from test/test_sshfs.py' >&2; exit 1;
}

# A mode-only source mutation must be detected by tree reconstruction.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cp -a "$ROOT/sshfs" "$tmp/sshfs"
chmod -x "$tmp/sshfs/test/test_sshfs.py"
if "$ROOT/tools/verify-sshfs-source.sh" --source-only "$tmp/sshfs" >/dev/null 2>&1; then
    echo 'source verifier accepted a mode-corrupted source tree' >&2
    exit 1
fi

echo 'source archive self-contained contract: PASS'
