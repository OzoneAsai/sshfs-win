#!/usr/bin/env bash
set -euo pipefail

root=${1:?usage: audit-runtime.sh ROOTDIR}
SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bindir="$root/bin"
origins="$root/etc/runtime-origins.tsv"
fail=0

if [[ -d "$root/etc/etc" ]]; then
    echo "FAIL package layout contains nested etc/etc" >&2
    fail=1
fi
if [[ ! -d "$bindir" ]]; then
    echo "FAIL runtime bin directory missing: $bindir" >&2
    exit 1
fi
if [[ ! -f "$origins" ]]; then
    echo "FAIL runtime origin map missing" >&2
    exit 1
fi

while IFS= read -r pat; do
    [[ -z "$pat" || "$pat" == \#* ]] && continue
    mapfile -t matches < <(find "$bindir" -maxdepth 1 -type f -iname "$pat" -print)
    if (( ${#matches[@]} )); then
        printf 'FORBIDDEN runtime dependency pattern %s:\n' "$pat" >&2
        printf '  %s\n' "${matches[@]}" >&2
        fail=1
    fi
done < "$SELF_DIR/deps/FORBIDDEN_RUNTIME_PATTERNS.txt"

declare -A seen
while IFS=$'\t' read -r base origin source; do
    [[ -n "$base" ]] || continue
    if [[ -n "${seen[$base]:-}" && "${seen[$base]}" != "$origin" ]]; then
        echo "FAIL conflicting provenance for $base" >&2
        fail=1
    fi
    seen[$base]=$origin
    case "$origin" in
        vendor:pcre2:7978954dbd2efc6f2196869290553cf1871b4ce6|\
        vendor:glib:43bc79ea8803e33c5eb368085e2d5906f9f98079|\
        vendor:openssl:f4dc4d58b48d346a8270183f89acf826d459b0ca|\
        vendor:openssh:b3f7344209832eea8ece447d871ea748767c444b|\
        cygwin-package:*)
            ;;
        local:sshfs-win|local:sshfs:24448e2493533ead984d6ca322c583e1a26cc613)
            ;;
        *)
            echo "FAIL unapproved runtime origin for $base: $origin" >&2
            fail=1
            ;;
    esac
done < "$origins"

while IFS= read -r -d '' f; do
    base=$(basename "$f")
    if [[ -z "${seen[$base]:-}" ]]; then
        echo "FAIL packaged file lacks provenance: $base" >&2
        fail=1
    fi
done < <(find "$bindir" -maxdepth 1 -type f -print0)

if find "$bindir" -maxdepth 1 -type f -iname '*pcre2*.dll' -print -quit | grep -q .; then
    grep -q 'vendor:pcre2:7978954dbd2efc6f2196869290553cf1871b4ce6' "$origins" || {
        echo 'FAIL PCRE2 runtime is not tied to the pinned 10.48 source' >&2
        fail=1
    }
fi
if find "$bindir" -maxdepth 1 -type f \( -iname '*glib*.dll' -o -iname '*gthread*.dll' \) -print -quit | grep -q .; then
    grep -q 'vendor:glib:43bc79ea8803e33c5eb368085e2d5906f9f98079' "$origins" || {
        echo 'FAIL GLib runtime is not tied to the pinned 2.88.3 source' >&2
        fail=1
    }
fi

# Inspect PE imports rather than allowing the build machine's PATH to hide
# missing bundled Cygwin DLLs. Inspect DLLs as well as executable seeds.
while IFS= read -r -d '' binary; do
    imports=$(objdump -p "$binary") || exit 1
    while IFS= read -r dependency; do
        if [[ ! -f "$bindir/$dependency" ]]; then
            echo "FAIL $(basename "$binary") requires missing $dependency" >&2
            fail=1
        fi
    done < <(printf '%s\n' "$imports" | awk '/DLL Name: cyg/ {print $3}' | tr -d '\r')
done < <(find "$bindir" -maxdepth 1 -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
exit "$fail"
