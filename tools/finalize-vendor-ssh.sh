#!/usr/bin/env bash
set -euo pipefail

root=${1:?build source root required}
SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=vendor-ssh-common.sh
. "$SELF_DIR/vendor-ssh-common.sh"

runtime="$root/.build/x64/vendor/runtime"
prefix="$root/.build/x64/vendor/prefix"
ssh_src="$root/.build/x64/vendor/src/openssh/$OPENSSH_BUILD_TARGET"
map="$root/.build/x64/vendor/vendor-source-map.tsv"

[[ -f "$ssh_src" ]] || {
    echo "built OpenSSH target missing: $ssh_src" >&2
    exit 2
}
mkdir -p "$runtime/bin"
touch "$map"
cp -f "$ssh_src" "$runtime/bin/ssh.exe"
vendor_ssh_register_file "$map" "$runtime/bin/ssh.exe" openssh "$OPENSSH_COMMIT"
vendor_ssh_register_openssl_runtime "$prefix" "$map"
vendor_ssh_record_metadata "$runtime" "$prefix"
cat "$runtime/openssh-version.txt"
