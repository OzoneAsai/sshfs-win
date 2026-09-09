#!/usr/bin/env bash

VENDOR_SSH_TOOLS_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
VENDOR_SSH_ROOT=$(cd -- "$VENDOR_SSH_TOOLS_DIR/.." && pwd)
VENDOR_SSH_LOCKS="$VENDOR_SSH_ROOT/deps/SOURCE_LOCKS.tsv"
VENDOR_SSH_SYSTEM_PATH=/usr/local/bin:/usr/bin

vendor_ssh_use_tool_path() {
    local prefix=$1
    export PATH="$prefix/bin:$VENDOR_SSH_SYSTEM_PATH"
}

vendor_ssh_lock_field() {
    local component=$1 field=$2
    awk -F '\t' -v component="$component" -v field="$field" '
        NR == 1 {
            for (i = 1; i <= NF; i++)
                column[$i] = i
            next
        }
        $1 == component {
            if (!(field in column))
                exit 3
            print $column[field]
            found = 1
            exit
        }
        END {
            if (!found)
                exit 4
        }
    ' "$VENDOR_SSH_LOCKS"
}

OPENSSL_REPO=https://github.com/openssl/openssl.git
OPENSSL_VERSION=$(vendor_ssh_lock_field OpenSSL version)
OPENSSL_REF=$(vendor_ssh_lock_field OpenSSL git_ref)
OPENSSL_COMMIT=$(vendor_ssh_lock_field OpenSSL commit)

OPENSSH_REPO=https://github.com/openssh/openssh-portable.git
OPENSSH_VERSION=$(vendor_ssh_lock_field "OpenSSH portable" version)
OPENSSH_REF=$(vendor_ssh_lock_field "OpenSSH portable" git_ref)
OPENSSH_COMMIT=$(vendor_ssh_lock_field "OpenSSH portable" commit)

# OpenSSH's explicit target is ssh$(EXEEXT). Under Cygwin EXEEXT=.exe.
# Keep this centralized so a recovery path cannot regress to GNU make's
# implicit "ssh <- ssh.o" rule.
OPENSSH_BUILD_TARGET=ssh.exe

vendor_ssh_clone_locked() {
    local repo=$1 ref=$2 commit=$3 dir=$4
    local actual

    rm -rf "$dir"
    git -c core.autocrlf=false clone --filter=blob:none --no-checkout "$repo" "$dir"
    git -C "$dir" fetch --depth=1 origin "refs/tags/$ref:refs/tags/$ref"
    git -C "$dir" checkout --detach "$commit"
    actual=$(git -C "$dir" rev-parse HEAD)
    [[ "$actual" == "$commit" ]] || {
        echo "SHA drift: $dir $actual != $commit" >&2
        return 20
    }
}

vendor_ssh_register_file() {
    local map=$1 file=$2 component=$3 commit=$4
    local base existing origin expected

    base=$(basename "$file")
    expected="vendor:$component:$commit"
    existing=$(awk -F '\t' -v b="$base" '$1 == b { print $0; exit }' "$map")
    if [[ -n "$existing" ]]; then
        origin=$(printf '%s\n' "$existing" | awk -F '\t' '{print $2}')
        [[ "$origin" == "$expected" ]] || {
            echo "vendor basename collision: $base ($origin vs $expected)" >&2
            return 24
        }
        return 0
    fi
    printf '%s\t%s\t%s\n' "$base" "$expected" "$file" >> "$map"
}

vendor_ssh_register_openssl_runtime() {
    local prefix=$1 map=$2
    local file

    while IFS= read -r -d '' file; do
        vendor_ssh_register_file "$map" "$file" openssl "$OPENSSL_COMMIT"
    done < <(find "$prefix/bin" -maxdepth 1 -type f \
        \( -iname 'cygssl-3*.dll' -o -iname 'cygcrypto-3*.dll' \) -print0 | sort -z)
}

vendor_ssh_record_metadata() {
    local runtime=$1 prefix=$2
    local version_tmp
    local ssh_exe="$runtime/bin/ssh.exe"
    local openssl_exe="$prefix/bin/openssl.exe"

    [[ -f "$ssh_exe" ]] || {
        echo "OpenSSH runtime missing: $ssh_exe" >&2
        return 25
    }
    [[ -f "$openssl_exe" ]] || {
        echo "OpenSSL runtime missing: $openssl_exe" >&2
        return 27
    }

    version_tmp=$(mktemp)
    if ! PATH="$prefix/bin:$VENDOR_SSH_SYSTEM_PATH" "$ssh_exe" -V >"$version_tmp" 2>&1; then
        echo "OpenSSH version probe failed: $ssh_exe -V" >&2
        cat "$version_tmp" >&2
        rm -f "$version_tmp"
        return 25
    fi
    tr -d '\r' <"$version_tmp" >"$runtime/openssh-version.txt"
    rm -f "$version_tmp"
    if ! grep -Fq "OpenSSH_${OPENSSH_VERSION}" "$runtime/openssh-version.txt"; then
        echo "OpenSSH version probe does not match SOURCE_LOCKS.tsv (${OPENSSH_VERSION}):" >&2
        cat "$runtime/openssh-version.txt" >&2
        return 26
    fi
    if ! grep -Fq "OpenSSL ${OPENSSL_VERSION%% *}" "$runtime/openssh-version.txt"; then
        echo "OpenSSH is not linked against the locked OpenSSL version (${OPENSSL_VERSION%% *}):" >&2
        cat "$runtime/openssh-version.txt" >&2
        return 29
    fi
    if ! PATH="$prefix/bin:$VENDOR_SSH_SYSTEM_PATH" "$openssl_exe" version -a \
        >"$runtime/openssl-version.txt"; then
        echo "OpenSSL version probe failed" >&2
        return 27
    fi
    [[ -s "$runtime/openssl-version.txt" ]] || {
        echo "OpenSSL version probe produced no output" >&2
        return 28
    }
    if ! grep -Fq "OpenSSL ${OPENSSL_VERSION%% *}" "$runtime/openssl-version.txt"; then
        echo "OpenSSL runtime does not match SOURCE_LOCKS.tsv (${OPENSSL_VERSION%% *})" >&2
        cat "$runtime/openssl-version.txt" >&2
        return 30
    fi

    printf '%s\n' "$OPENSSL_COMMIT" >"$runtime/openssl.commit"
    printf '%s\n' "$OPENSSH_COMMIT" >"$runtime/openssh.commit"
}

vendor_ssh_build_openssh() {
    local src=$1 prefix=$2 runtime=$3 map=$4 jobs=$5
    local openssh_dir="$src/openssh"

    vendor_ssh_clone_locked "$OPENSSH_REPO" "$OPENSSH_REF" "$OPENSSH_COMMIT" "$openssh_dir"
    (
        vendor_ssh_use_tool_path "$prefix"
        cd "$openssh_dir"
        [[ -x ./configure ]] || autoreconf -fi
        CPPFLAGS="-I$prefix/include" \
        LDFLAGS="-L$prefix/lib" \
        ./configure --prefix="$prefix/openssh" --with-ssl-dir="$prefix"

        # -r disables GNU make's built-in implicit rules. If the explicit
        # Cygwin target ever changes, fail immediately rather than linking
        # ssh.o as a bogus standalone program.
        make -r -j"$jobs" "$OPENSSH_BUILD_TARGET"
        mkdir -p "$runtime/bin"
        cp "./$OPENSSH_BUILD_TARGET" "$runtime/bin/ssh.exe"
    )

    vendor_ssh_register_file "$map" "$runtime/bin/ssh.exe" openssh "$OPENSSH_COMMIT"
    vendor_ssh_record_metadata "$runtime" "$prefix"
}
