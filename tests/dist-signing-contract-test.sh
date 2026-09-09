#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/tools" "$tmp/mock/bin" "$tmp/.build/x64/status" "$tmp/.build/x64/wix"
cp "$project_root/Makefile" "$tmp/Makefile"
cp "$project_root/VERSION" "$tmp/VERSION"

cat > "$tmp/mock/bin/cygpath" <<'EOF'
#!/usr/bin/env bash
set -e
for arg in "$@"; do last=$arg; done
printf '%s\n' "$last"
EOF
chmod +x "$tmp/mock/bin/cygpath"

cat > "$tmp/tools/signtool" <<'EOF'
#!/usr/bin/env bash
exit "${SIGN_RC:-1}"
EOF
chmod +x "$tmp/tools/signtool"

version=$(tr -d '\r\n' < "$tmp/VERSION")
src="$tmp/.build/x64/wix/sshfs-win-${version}-x64.msi"
dst="$tmp/.build/x64/dist/sshfs-win-${version}-x64.msi"
status="$tmp/.build/x64/status/dist"
printf 'fixture-msi\n' > "$src"
touch "$tmp/.build/x64/status/wix"

run_make() {
    (
        cd "$tmp"
        PATH="$tmp/mock/bin:$PATH" WIX="$tmp/mock" "$@" make -f Makefile ".build/x64/status/dist"
    )
}

# Signing failure must not publish a final artifact or completion marker.
# A stale artifact from an earlier attempt must also be removed before signing.
rm -f "$dst" "$status" "$dst.signing"
mkdir -p "$(dirname "$dst")"
printf 'stale-msi\n' > "$dst"
if SIGN_RC=1 PATH="$tmp/mock/bin:$PATH" WIX="$tmp/mock" \
    make -C "$tmp" -f Makefile -o ".build/x64/status/wix" ".build/x64/status/dist" >/dev/null 2>&1; then
    echo "signing failure unexpectedly succeeded" >&2
    exit 1
fi
[[ ! -e "$dst" && ! -e "$status" && ! -e "$dst.signing" ]] || {
    echo "failed signing leaked a distribution artifact or status marker" >&2
    exit 2
}

# Deliberate local unsigned build is the explicit escape hatch.
SIGN_RC=1 PATH="$tmp/mock/bin:$PATH" WIX="$tmp/mock" \
    make -C "$tmp" -f Makefile -o ".build/x64/status/wix" AllowUnsigned=1 ".build/x64/status/dist" >/dev/null
[[ -f "$dst" && -f "$status" ]] || {
    echo "AllowUnsigned=1 did not publish the local unsigned artifact" >&2
    exit 3
}
cmp -s "$src" "$dst" || {
    echo "unsigned artifact changed unexpectedly" >&2
    exit 4
}

# A signed build also publishes only after the signing command succeeds.
rm -f "$dst" "$status"
SIGN_RC=0 PATH="$tmp/mock/bin:$PATH" WIX="$tmp/mock" \
    make -C "$tmp" -f Makefile -o ".build/x64/status/wix" ".build/x64/status/dist" >/dev/null
[[ -f "$dst" && -f "$status" ]] || {
    echo "successful signing did not publish artifact" >&2
    exit 5
}

echo "distribution signing contract: PASS"
