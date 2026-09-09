#!/usr/bin/env bash
set -euo pipefail
root=${1:?build source root required}
cd "$root"
prefix=.build/x64/vendor/prefix
dest=.build/x64/root/bin
origins=.build/x64/root/etc/runtime-origins.tsv
map=.build/x64/vendor/vendor-source-map.tsv
while IFS=$'\t' read -r base origin source; do
    [[ -f "$source" ]] || continue
    cp -f "$source" "$dest/$base"
    if ! awk -F $'\t' -v b="$base" '$1 == b { found=1 } END { exit !found }' "$origins"; then
        printf '%s\t%s\t%s\n' "$base" "$origin" "$source" >> "$origins"
    fi
done < <(awk -F $'\t' '$2 ~ /^vendor:glib:/ { print }' "$map")
while IFS=$'\t' read -r base origin source; do
    [[ -f "$source" ]] || continue
    cp -f "$source" "$dest/$base"
    if ! awk -F $'\t' -v b="$base" '$1 == b { found=1 } END { exit !found }' "$origins"; then
        printf '%s\t%s\t%s\n' "$base" "$origin" "$source" >> "$origins"
    fi
done < <(awk -F $'\t' '$2 ~ /^vendor:pcre2:/ { print }' "$map")
tools/write-dependency-manifest.sh .build/x64/root
tools/audit-runtime.sh .build/x64/root
