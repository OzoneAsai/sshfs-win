#!/usr/bin/env bash
set -euo pipefail

cd "$1"
PREFIX=$PWD/.build/x64/vendor/prefix
SRC=$PWD/.build/x64/vendor/src
RUNTIME=$PWD/.build/x64/vendor/runtime
MAP=$PWD/.build/x64/vendor/vendor-source-map.tsv

meson install -C "$SRC/glib/build"
while IFS= read -r -d '' file; do
    base=$(basename "$file")
    if ! awk -F '\t' -v base="$base" '$1 == base { found=1 } END { exit !found }' "$MAP"; then
        printf '%s\t%s\t%s\n' "$base" \
            'vendor:glib:43bc79ea8803e33c5eb368085e2d5906f9f98079' "$file" >> "$MAP"
    fi
done < <(find "$PREFIX/bin" -maxdepth 1 -type f -iname '*.dll' -print0)

mkdir -p "$RUNTIME"
printf '%s\n' 7978954dbd2efc6f2196869290553cf1871b4ce6 > "$RUNTIME/pcre2.commit"
printf '%s\n' 43bc79ea8803e33c5eb368085e2d5906f9f98079 > "$RUNTIME/glib.commit"
printf '%s\n' 10.48 > "$RUNTIME/pcre2-version.txt"
printf '%s\n' 2.88.3 > "$RUNTIME/glib-version.txt"
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --modversion glib-2.0
