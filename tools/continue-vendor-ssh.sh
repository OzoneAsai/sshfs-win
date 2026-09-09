#!/usr/bin/env bash
set -euo pipefail

root=${1:?usage: continue-vendor-ssh.sh /path/to/sshfs-win-build-root}
cd "$root"

SELF_DIR="$PWD/tools"
# shellcheck source=vendor-ssh-common.sh
. "$SELF_DIR/vendor-ssh-common.sh"

OUT=$PWD/.build/x64/vendor
PREFIX=$OUT/prefix
RUNTIME=$OUT/runtime
SRC=$OUT/src
MAP=$OUT/vendor-source-map.tsv
JOBS=${NUMBER_OF_PROCESSORS:-4}

[[ -d "$SRC/openssl" ]] || {
    echo "OpenSSL source tree missing: $SRC/openssl" >&2
    exit 2
}
mkdir -p "$RUNTIME/bin"
touch "$MAP"
vendor_ssh_use_tool_path "$PREFIX"

make -C "$SRC/openssl" install_sw
vendor_ssh_register_openssl_runtime "$PREFIX" "$MAP"
vendor_ssh_build_openssh "$SRC" "$PREFIX" "$RUNTIME" "$MAP" "$JOBS"
