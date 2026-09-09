#!/usr/bin/env bash
set -euo pipefail

SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=vendor-ssh-common.sh
. "$SELF_DIR/vendor-ssh-common.sh"

OUT=${1:-.build/vendor}
OUT=$(mkdir -p "$OUT" && cd "$OUT" && pwd)
SRC="$OUT/src"
PREFIX="$OUT/prefix"
RUNTIME="$OUT/runtime"
MAP="$OUT/vendor-source-map.tsv"
JOBS=${NUMBER_OF_PROCESSORS:-4}
mkdir -p "$SRC" "$PREFIX" "$RUNTIME/bin"
touch "$MAP"
vendor_ssh_use_tool_path "$PREFIX"

vendor_ssh_clone_locked "$OPENSSL_REPO" "$OPENSSL_REF" "$OPENSSL_COMMIT" "$SRC/openssl"
(
    vendor_ssh_use_tool_path "$PREFIX"
    cd "$SRC/openssl"
    ./Configure Cygwin-x86_64 shared --prefix="$PREFIX" --openssldir="$PREFIX/ssl"
    make -j"$JOBS"
    make test
    make install_sw
)
vendor_ssh_register_openssl_runtime "$PREFIX" "$MAP"

vendor_ssh_build_openssh "$SRC" "$PREFIX" "$RUNTIME" "$MAP" "$JOBS"

echo "$RUNTIME"
