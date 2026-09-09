#!/usr/bin/env bash
set -euo pipefail

root=${1:?usage: copy-runtime-closure.sh ROOT VENDOR_DIR BINARY...}
vendor=${2:?usage: copy-runtime-closure.sh ROOT VENDOR_DIR BINARY...}
shift 2
(( $# > 0 )) || { echo "no binaries supplied" >&2; exit 2; }

SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=$(cd -- "$vendor/prefix" && pwd)
map="$vendor/vendor-source-map.tsv"
origins="$root/etc/runtime-origins.tsv"
mkdir -p "$root/bin" "$root/etc"
touch "$origins"

copy_one() {
    local source=$1 origin=$2
    local base dst src_hash dst_hash existing
    base=$(basename "$source")
    if [[ "$base" != *.exe && -f "$source.exe" ]]; then
        source="$source.exe"
        base="$base.exe"
    fi
    dst="$root/bin/$base"
    src_hash=$(sha256sum "$source" | awk '{print $1}')
    if [[ -f "$dst" ]]; then
        dst_hash=$(sha256sum "$dst" | awk '{print $1}')
        [[ "$dst_hash" == "$src_hash" ]] || {
            echo "runtime basename collision with different bytes: $base" >&2
            echo "  existing=$dst_hash source=$src_hash ($source)" >&2
            exit 30
        }
    else
        cp -f "$source" "$dst"
    fi
    existing=$(awk -F '\t' -v b="$base" '$1 == b {print $2; exit}' "$origins")
    if [[ -n "$existing" && "$existing" != "$origin" ]]; then
        echo "runtime provenance collision: $base ($existing vs $origin)" >&2
        exit 31
    fi
    if [[ -z "$existing" ]]; then
        printf '%s\t%s\t%s\n' "$base" "$origin" "$source" >> "$origins"
    fi
}


lock_commit() {
    local component=$1
    awk -F '\t' -v c="$component" '$1 == c { print $4; exit }' "$SELF_DIR/deps/SOURCE_LOCKS.tsv"
}

cygwin_package_owner() {
    local source=$1 output owner
    if ! output=$(cygcheck -f "$source" 2>&1); then
        echo "cygcheck package lookup failed for $source: $output" >&2
        return 1
    fi
    owner=$(printf '%s\n' "$output" | tr -d '\r' | head -n1)
    if [[ -z "$owner" ]]; then
        echo "cygcheck package lookup returned no owner for $source" >&2
        return 1
    fi
    printf '%s\n' "$owner"
}

seed_origin() {
    local source=$1 base origin owner commit
    base=$(basename "$source")
    origin=$(vendor_origin "$source")
    if [[ -n "$origin" ]]; then
        printf '%s\n' "$origin"
        return 0
    fi
    case "$base" in
        sshfs|sshfs.exe)
            commit=$(lock_commit "libfuse/sshfs")
            [[ -n "$commit" ]] || return 1
            printf 'local:sshfs:%s\n' "$commit"
            ;;
        sshfs-win|sshfs-win.exe)
            printf '%s\n' 'local:sshfs-win'
            ;;
        *)
            case "$source" in
                /usr/bin/*)
                    owner=$(cygwin_package_owner "$source") || return 1
                    printf 'cygwin-package:%s\n' "$owner"
                    ;;
                *)
                    return 1
                    ;;
            esac
            ;;
    esac
}

vendor_origin() {
    local source=$1 base
    base=$(basename "$source")
    awk -F '\t' -v b="$base" '$1 == b {print $2; exit}' "$map"
}

resolve_and_copy() {
    local binary=$1 dep owner origin raw_list normalized_list p converted
    [[ -f "$binary" ]] || { echo "runtime seed missing: $binary" >&2; exit 32; }

    raw_list=$(mktemp)
    normalized_list=$(mktemp)
    if ! PATH="$prefix/bin:$PATH" cygcheck "$binary" >"$raw_list" 2>&1; then
        echo "dependency discovery failed for $binary" >&2
        cat "$raw_list" >&2
        rm -f "$raw_list" "$normalized_list"
        exit 36
    fi

    while IFS= read -r p; do
        p=${p//$'\r'/}
        p=${p#"${p%%[![:space:]]*}"}
        [[ -n "$p" ]] || continue
        if ! converted=$(cygpath -au "$p" 2>&1); then
            echo "cannot normalize dependency path for $binary: $p" >&2
            echo "$converted" >&2
            rm -f "$raw_list" "$normalized_list"
            exit 37
        fi
        printf '%s\n' "$converted" >>"$normalized_list"
    done <"$raw_list"
    rm -f "$raw_list"

    sort -u -o "$normalized_list" "$normalized_list"
    while IFS= read -r dep; do
        [[ -f "$dep" ]] || {
            echo "dependency reported by cygcheck is missing: $dep (seed $binary)" >&2
            rm -f "$normalized_list"
            exit 38
        }
        case "$dep" in
            "$prefix"/bin/*)
                origin=$(vendor_origin "$dep")
                [[ -n "$origin" ]] || {
                    echo "vendor runtime lacks provenance: $dep" >&2
                    rm -f "$normalized_list"
                    exit 33
                }
                copy_one "$dep" "$origin"
                ;;
            /usr/bin/*)
                owner=$(cygwin_package_owner "$dep") || {
                    rm -f "$normalized_list"
                    exit 34
                }
                copy_one "$dep" "cygwin-package:$owner"
                ;;
            *)
                # Windows system DLLs are intentionally not bundled.
                ;;
        esac
    done <"$normalized_list"
    rm -f "$normalized_list"
}

for binary in "$@"; do
    if ! origin=$(seed_origin "$binary"); then
        echo "runtime seed lacks approved provenance: $binary" >&2
        exit 35
    fi
    copy_one "$binary" "$origin"
    resolve_and_copy "$binary"
done
