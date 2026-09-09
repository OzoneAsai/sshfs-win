#!/usr/bin/env bash
set -euo pipefail

EXPECTED_COMMIT=24448e2493533ead984d6ca322c583e1a26cc613
EXPECTED_TREE=35655f60d37403663238a73b4204cfd64fdca73c
EXPECTED_MAIN_BLOB=3bd4ec57b3bf7c5eb8aff68dafa6a84264186269
SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
EXECUTABLES="$SELF_DIR/deps/SSHFS_EXECUTABLES.txt"
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
if [[ ! -f "$EXECUTABLES" ]]; then
    echo "missing canonical SSHFS executable manifest: $EXECUTABLES" >&2
    exit 2
fi

main_blob=$(git -c core.autocrlf=false hash-object --no-filters "$SRC/sshfs.c")
if [[ "$main_blob" != "$EXPECTED_MAIN_BLOB" ]]; then
    echo "wrong pristine sshfs.c blob: expected $EXPECTED_MAIN_BLOB, got $main_blob" >&2
    exit 4
fi

# On native POSIX filesystems, verify the observed execute bits as a separate
# source-archive contract. Cygwin/MSYS/MINGW map NTFS ACLs to POSIX modes and
# cannot reliably preserve Git's executable bit through a Windows checkout, so
# their observed mode is not source identity. The canonical Git mode is still
# enforced below on every platform.
platform=$(uname -s 2>/dev/null || printf 'unknown')
case "$platform" in
    CYGWIN*|MSYS*|MINGW*)
        echo "filesystem executable-mode observation: SKIP ($platform; canonical modes still enforced)"
        ;;
    *)
        while IFS= read -r -d '' file; do
            rel=${file#"$SRC"/}
            expected_exec=0
            if grep -Fvx '#' "$EXECUTABLES" | grep -Fxq -- "$rel"; then
                expected_exec=1
            fi
            actual_exec=0
            [[ -x "$file" ]] && actual_exec=1
            if (( actual_exec != expected_exec )); then
                expected_mode=100644
                (( expected_exec )) && expected_mode=100755
                echo "wrong SSHFS executable mode: $rel expected $expected_mode" >&2
                exit 5
            fi
        done < <(find "$SRC" -type f -print0)
        echo "filesystem executable-mode observation: PASS"
        ;;
esac

# Reconstruct a canonical Git tree from bytes plus the pinned executable-mode
# manifest. Never let host ACL/mode emulation or global autocrlf settings alter
# source identity. All regular files enter the index as 100644; the seven
# manifest paths are then promoted to 100755 before write-tree.
meta=$(mktemp -d)
work=""
cleanup() {
    [[ -z "$work" ]] || rm -rf "$work"
    rm -rf "$meta"
}
trap cleanup EXIT HUP INT TERM

git -C "$meta" init -q
git -C "$meta" config core.autocrlf false
git -C "$meta" config core.filemode false
GIT_WORK_TREE="$SRC" git -C "$meta" add -A
while IFS= read -r -d '' path; do
    GIT_WORK_TREE="$SRC" git -C "$meta" update-index --chmod=-x -- "$path"
done < <(git -C "$meta" ls-files -z)
while IFS= read -r path || [[ -n "$path" ]]; do
    [[ -z "$path" || "$path" == \#* ]] && continue
    if ! git -C "$meta" ls-files --error-unmatch -- "$path" >/dev/null 2>&1; then
        echo "canonical executable path missing from SSHFS source: $path" >&2
        exit 5
    fi
    GIT_WORK_TREE="$SRC" git -C "$meta" update-index --chmod=+x -- "$path"
done < "$EXECUTABLES"

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

patched_blob=$(git -c core.autocrlf=false hash-object --no-filters "$work/sshfs.c")
printf 'patched_sshfs_c_blob=%s\n' "$patched_blob"
echo "exact SSHFS patch chain: PASS"
