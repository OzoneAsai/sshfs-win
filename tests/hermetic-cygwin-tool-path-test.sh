#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

makefile=$(cat "$ROOT/Makefile")
[[ "$makefile" == *'CoreBuildPath = $(abspath $(VendorDir))/prefix/bin:/usr/local/bin:/usr/bin'* ]] || {
    echo 'Makefile core build path is not hermetic' >&2
    exit 1
}
[[ "$makefile" != *'CoreBuildPath = '*'$PATH'* ]] || {
    echo 'Makefile core build path inherits host PATH' >&2
    exit 1
}

vendor_core=$(cat "$ROOT/tools/build-vendor-core.sh")
[[ "$vendor_core" == *'TOOL_PATH="$PREFIX/bin:/usr/local/bin:/usr/bin"'* ]] || {
    echo 'vendor core build lacks explicit Cygwin tool path' >&2
    exit 1
}
[[ "$vendor_core" == *'export PATH="$TOOL_PATH"'* ]] || {
    echo 'vendor core build does not activate explicit tool path' >&2
    exit 1
}
[[ "$vendor_core" != *'PATH="$PREFIX/bin:$PATH"'* ]] || {
    echo 'vendor core build reintroduces inherited host PATH' >&2
    exit 1
}

vendor_common=$(cat "$ROOT/tools/vendor-ssh-common.sh")
[[ "$vendor_common" == *'VENDOR_SSH_SYSTEM_PATH=/usr/local/bin:/usr/bin'* ]] || {
    echo 'vendor SSH model lacks explicit Cygwin system path' >&2
    exit 1
}
[[ "$vendor_common" == *'export PATH="$prefix/bin:$VENDOR_SSH_SYSTEM_PATH"'* ]] || {
    echo 'vendor SSH model does not activate explicit tool path' >&2
    exit 1
}

for file in build-vendor-ssh.sh continue-vendor-ssh.sh finalize-vendor-ssh.sh; do
    text=$(cat "$ROOT/tools/$file")
    [[ "$text" == *'vendor_ssh_use_tool_path'* ]] || {
        echo "$file does not enter the shared hermetic tool path" >&2
        exit 1
    }
done

echo 'hermetic Cygwin tool path contract: PASS'
