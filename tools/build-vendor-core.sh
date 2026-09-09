#!/usr/bin/env bash
set -euo pipefail

PCRE2_REPO=https://github.com/PCRE2Project/pcre2.git
PCRE2_REF=pcre2-10.48
PCRE2_COMMIT=7978954dbd2efc6f2196869290553cf1871b4ce6
GLIB_REPO=https://github.com/GNOME/glib.git
GLIB_REF=2.88.3
GLIB_COMMIT=43bc79ea8803e33c5eb368085e2d5906f9f98079

OUT=${1:-.build/vendor}
OUT=$(mkdir -p "$OUT" && cd "$OUT" && pwd)
SRC="$OUT/src"
PREFIX="$OUT/prefix"
RUNTIME="$OUT/runtime"
MAP="$OUT/vendor-source-map.tsv"
JOBS=${NUMBER_OF_PROCESSORS:-4}
mkdir -p "$SRC" "$PREFIX" "$RUNTIME/bin"
: > "$MAP"

# This is a Cygwin-native toolchain. Do not allow Windows-host helpers from the
# inherited PATH (notably ccache on hosted runners) to become compiler launchers.
TOOL_PATH="$PREFIX/bin:/usr/local/bin:/usr/bin"
export PATH="$TOOL_PATH"

clone_locked() {
    local repo=$1 ref=$2 commit=$3 dir=$4
    rm -rf "$dir"
    git -c core.autocrlf=false clone --filter=blob:none --no-checkout "$repo" "$dir"
    git -C "$dir" fetch --depth=1 origin "refs/tags/$ref:refs/tags/$ref"
    git -C "$dir" checkout --detach "$commit"
    local actual
    actual=$(git -C "$dir" rev-parse HEAD)
    [[ "$actual" == "$commit" ]] || {
        echo "SHA drift: $dir $actual != $commit" >&2
        exit 20
    }
}

register_prefix_dlls() {
    local component=$1 commit=$2
    while IFS= read -r -d '' f; do
        local base existing origin expected
        base=$(basename "$f")
        existing=$(awk -F '\t' -v b="$base" '$1 == b { print $0 }' "$MAP")
        expected="vendor:$component:$commit"
        if [[ -n "$existing" ]]; then
            origin=$(printf '%s\n' "$existing" | awk -F '\t' 'NR == 1 { print $2 }')
            if [[ "$origin" != "$expected" ]]; then
                printf 'vendor basename collision: %s (%s vs %s)\n' \
                    "$base" "$origin" "$expected" >&2
                exit 24
            fi
            continue
        fi
        printf '%s\t%s\t%s\n' "$base" "$expected" "$f" >> "$MAP"
    done < <(find "$PREFIX/bin" -maxdepth 1 -type f -iname '*.dll' -print0 | sort -z)
}

clone_locked "$PCRE2_REPO" "$PCRE2_REF" "$PCRE2_COMMIT" "$SRC/pcre2"
cmake -S "$SRC/pcre2" -B "$SRC/pcre2/build" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_LIBS=OFF \
    -DPCRE2_BUILD_PCRE2_8=ON \
    -DPCRE2_BUILD_PCRE2_16=OFF \
    -DPCRE2_BUILD_PCRE2_32=OFF \
    -DPCRE2_BUILD_PCRE2GREP=OFF \
    -DPCRE2_BUILD_TESTS=ON
cmake --build "$SRC/pcre2/build" --parallel "$JOBS"
ctest --test-dir "$SRC/pcre2/build" --output-on-failure
cmake --install "$SRC/pcre2/build"
register_prefix_dlls pcre2 "$PCRE2_COMMIT"

PCRE2_PKGCONFIG="$PREFIX/lib/pkgconfig"
[[ "$(PKG_CONFIG_PATH="$PCRE2_PKGCONFIG" pkg-config --modversion libpcre2-8)" == 10.48 ]] || {
    echo "PCRE2 provenance failure: vendor 10.48 is not first in pkg-config resolution" >&2
    exit 21
}

clone_locked "$GLIB_REPO" "$GLIB_REF" "$GLIB_COMMIT" "$SRC/glib"
rm -rf "$SRC/glib/build"
PKG_CONFIG_PATH="$PCRE2_PKGCONFIG" \
meson setup "$SRC/glib/build" "$SRC/glib" \
    --prefix="$PREFIX" \
    --wrap-mode=nodownload \
    --default-library=shared \
    -Dtests=true \
    -Dinstalled_tests=false \
    -Dintrospection=disabled \
    -Ddocumentation=false \
    -Dman-pages=disabled \
    -Dselinux=disabled \
    -Dlibmount=disabled \
    -Dsysprof=disabled \
    -Dnls=disabled
PKG_CONFIG_PATH="$PCRE2_PKGCONFIG" \
    meson compile -C "$SRC/glib/build" -j "$JOBS"
PKG_CONFIG_PATH="$PCRE2_PKGCONFIG" \
    meson test -C "$SRC/glib/build" --print-errorlogs
PKG_CONFIG_PATH="$PCRE2_PKGCONFIG" \
    meson install -C "$SRC/glib/build"
register_prefix_dlls glib "$GLIB_COMMIT"

GLIB_PC="$PREFIX/lib/pkgconfig"
[[ "$(PKG_CONFIG_PATH="$GLIB_PC" pkg-config --modversion glib-2.0)" == 2.88.3 ]] || {
    echo "GLib provenance failure: expected 2.88.3 from vendor prefix" >&2
    exit 22
}
[[ "$(PKG_CONFIG_PATH="$GLIB_PC" pkg-config --modversion libpcre2-8)" == 10.48 ]] || {
    echo "PCRE2 provenance failure after GLib install" >&2
    exit 23
}

printf '%s\n' "$PCRE2_COMMIT" > "$RUNTIME/pcre2.commit"
printf '%s\n' "$GLIB_COMMIT" > "$RUNTIME/glib.commit"
printf '%s\n' "10.48" > "$RUNTIME/pcre2-version.txt"
printf '%s\n' "2.88.3" > "$RUNTIME/glib-version.txt"

echo "$PREFIX"
