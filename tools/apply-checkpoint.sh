#!/usr/bin/env bash
set -euo pipefail

BASE_SHA=5dda8ab2aa5a2b9edaa17802138c17bdfb166eed
SSHFS_SHA=24448e2493533ead984d6ca322c583e1a26cc613
SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
REPO=${1:-}

if [[ -z "$REPO" || ! -d "$REPO/.git" ]]; then
    echo "usage: $0 /path/to/winfsp-sshfs-win-checkout" >&2
    exit 2
fi

actual=$(git -C "$REPO" rev-parse HEAD)
if [[ "$actual" != "$BASE_SHA" ]]; then
    echo "refusing to apply: expected base $BASE_SHA, got $actual" >&2
    exit 3
fi
if [[ -n "$(git -C "$REPO" status --porcelain --untracked-files=no --ignore-submodules=all)" ]]; then
    echo "refusing to apply over tracked local changes" >&2
    exit 4
fi

# The checkpoint owns a verified pristine SSHFS source tree. Do not require the
# target checkout to have initialized submodule metadata or the pinned commit in
# a nested Git object store; replace that historical representation with the
# checkpoint's self-contained vendored source instead.
"$SELF_DIR/tools/stage-sshfs-source.sh" "$SELF_DIR/sshfs" "$REPO/sshfs"
rm -f -- "$REPO/.gitmodules"

cp "$SELF_DIR/Makefile" "$REPO/Makefile"
cp "$SELF_DIR/VERSION" "$REPO/VERSION"
cp "$SELF_DIR/CHECKPOINT" "$REPO/CHECKPOINT"
cp "$SELF_DIR/README.md" "$REPO/README.md"
cp "$SELF_DIR/sshfs-win.c" "$REPO/sshfs-win.c"
cp "$SELF_DIR/portable-format.h" "$REPO/portable-format.h"
cp "$SELF_DIR/windows-commandline-parser.h" "$REPO/windows-commandline-parser.h"
cp "$SELF_DIR/sshfs-win.wxs" "$REPO/sshfs-win.wxs"
cp "$SELF_DIR/sshfs-win.wixproj" "$REPO/sshfs-win.wixproj"
rm -f -- "$REPO/GroupReadWrite.reg" "$REPO/ServerAliveInterval.reg"

mkdir -p "$REPO/etc"
cp "$SELF_DIR/etc/nsswitch.conf" "$REPO/etc/nsswitch.conf"
cp "$SELF_DIR/etc/ssh_config" "$REPO/etc/ssh_config"
cp "$SELF_DIR/DIAGNOSTICS.md" "$REPO/DIAGNOSTICS.md"
cp "$SELF_DIR/DEPENDENCY_POLICY.md" "$REPO/DEPENDENCY_POLICY.md"
cp "$SELF_DIR/FORKLESS_SPAWN.md" "$REPO/FORKLESS_SPAWN.md"
cp "$SELF_DIR/UNICODE_WINDOWS_ARGS.md" "$REPO/UNICODE_WINDOWS_ARGS.md"
cp "$SELF_DIR/WINDOWS11_PREFLIGHT.md" "$REPO/WINDOWS11_PREFLIGHT.md"
cp "$SELF_DIR/LAUNCHER_READINESS.md" "$REPO/LAUNCHER_READINESS.md"
cp "$SELF_DIR/WINDOWS11_COMPAT.md" "$REPO/WINDOWS11_COMPAT.md"
cp "$SELF_DIR/WINDOWS_UX_CONTRACT.md" "$REPO/WINDOWS_UX_CONTRACT.md"
cp "$SELF_DIR/WRITE_SAFETY.md" "$REPO/WRITE_SAFETY.md"

mkdir -p "$REPO/deps"
cp "$SELF_DIR"/deps/* "$REPO/deps/"
mkdir -p "$REPO/tools"
cp -p "$SELF_DIR"/tools/* "$REPO/tools/"

mkdir -p "$REPO/tests"
find "$SELF_DIR/tests" -maxdepth 1 -type f \
    \( -name '*.c' -o -name '*.inc' -o -name '*.sh' -o -name '*.py' \) \
    -exec cp -p {} "$REPO/tests/" \;

mkdir -p "$REPO/.github/workflows"
cp "$SELF_DIR/.github/workflows/contracts.yml" "$REPO/.github/workflows/contracts.yml"

mkdir -p "$REPO/patches"
rm -f "$REPO/patches/30-stdiomode.patch" "$REPO/patches/10-mountpoint.patch" "$REPO/patches/20-mountmgr.patch"
cp "$SELF_DIR"/patches/*.patch "$REPO/patches/"
cp "$SELF_DIR/patches/SERIES" "$REPO/patches/SERIES"

shopt -s nullglob
top_level_patches=("$SELF_DIR"/top-level-patches/*.patch)
shopt -u nullglob
for p in "${top_level_patches[@]}"; do
    # The canonical WiX source replaces the historical incremental patches.
    if grep -q '^--- a/sshfs-win.wxs' "$p"; then continue; fi
    git -C "$REPO" apply --ignore-space-change --check "$p"
    git -C "$REPO" apply --ignore-space-change "$p"
done

echo "checkpoint applied"
echo "base:  $BASE_SHA"
echo "sshfs: $SSHFS_SHA (vendored verified source)"
echo "stage the transformed tree with: git -C '$REPO' add -A"