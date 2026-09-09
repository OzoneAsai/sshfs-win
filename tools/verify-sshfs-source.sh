#!/usr/bin/env bash
set -euo pipefail

EXPECTED_COMMIT=24448e2493533ead984d6ca322c583e1a26cc613
EXPECTED_TREE=35655f60d37403663238a73b4204cfd64fdca73c
EXPECTED_MAIN_BLOB=3bd4ec57b3bf7c5eb8aff68dafa6a84264186269
SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SOURCE_ONLY=0

if [[ ${1:-} == --source-only ]]; then
    SOURCE_ONLY=1
    shift
fi
SRC=${1:-}

if [[ -z "$SRC" || ! -d "$SRC" ]]; then
    echo "usage: $0 [--source-only] /path/to/libfuse-sshfs-source" >&2
    exit 2
fi
SRC=$(cd -- "$SRC" && pwd)
if [[ ! -f "$SRC/sshfs.c" ]]; then
    echo "not an SSHFS source tree: missing sshfs.c" >&2
    exit 2
fi

main_blob=$(git hash-object "$SRC/sshfs.c")
if [[ "$main_blob" != "$EXPECTED_MAIN_BLOB" ]]; then
    echo "wrong pristine sshfs.c blob: expected $EXPECTED_MAIN_BLOB, got $main_blob" >&2
    exit 4
fi

# Reconstruct the Git tree from a plain source directory. This deliberately
# does not require nested .git metadata, so released/checkpoint source archives
# remain self-contained. File modes are part of the tree identity.
meta=$(mktemp -d)
work=""
cleanup() {
    [[ -z "$work" ]] || rm -rf "$work"
    rm -rf "$meta"
}
trap cleanup EXIT HUP INT TERM

git -C "$meta" init -q
GIT_WORK_TREE="$SRC" git -C "$meta" add -A
tree=$(git -C "$meta" write-tree)
if [[ "$tree" != "$EXPECTED_TREE" ]]; then
    echo "wrong SSHFS source tree: expected $EXPECTED_TREE, got $tree" >&2
    exit 5
fi

if [[ -d "$SRC/.git" ]]; then
    actual=$(git -C "$SRC" rev-parse HEAD)
    if [[ "$actual" != "$EXPECTED_COMMIT" ]]; then
        echo "wrong SSHFS commit: expected $EXPECTED_COMMIT, got $actual" >&2
        exit 3
    fi
fi

printf 'base_commit=%s\nbase_tree=%s\nbase_sshfs_c_blob=%s\n' \
    "$EXPECTED_COMMIT" "$tree" "$main_blob"
echo "pristine SSHFS source identity: PASS"

if (( SOURCE_ONLY )); then
    exit 0
fi

work=$(mktemp -d)
git -C "$meta" archive --format=tar "$tree" | tar -xf - -C "$work"

series="$SELF_DIR/patches/SERIES"
[[ -f "$series" ]] || { echo "missing patch series manifest: $series" >&2; exit 6; }
while IFS= read -r patch_name || [[ -n "$patch_name" ]]; do
    patch_name=${patch_name%$'\r'}
    [[ -z "$patch_name" || "$patch_name" == \#* ]] && continue
    patch_file="$SELF_DIR/patches/$patch_name"
    [[ -f "$patch_file" ]] || { echo "missing patch listed in SERIES: $patch_name" >&2; exit 6; }
    normalized=$(mktemp)
    sed 's/\r$//' <"$patch_file" >"$normalized"
    echo "checking $patch_name"
    (cd "$work" && git apply --check --whitespace=error-all "$normalized")
    (cd "$work" && git apply --whitespace=error-all "$normalized")
    rm -f "$normalized"
done < "$series"

if grep -RIl $'\xEF\xBB\xBF' "$work" | grep -q .; then
    echo "BOM introduced by SSHFS patch chain" >&2
    exit 6
fi

patched_blob=$(git hash-object "$work/sshfs.c")
printf 'patched_sshfs_c_blob=%s\n' "$patched_blob"
echo "exact SSHFS patch chain: PASS"
