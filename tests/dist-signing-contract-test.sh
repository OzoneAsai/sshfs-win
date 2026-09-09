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
if [[ -n "${SIGN_ARGS:-}" ]]; then
    printf '%s\n' "$@" > "$SIGN_ARGS"
fi
exit "${SIGN_RC:-1}"
EOF
chmod +x "$tmp/tools/signtool"

version=$(tr -d '\r\n' < "$tmp/VERSION")
src="$tmp/.build/x64/wix/sshfs-win-${version}-x64.msi"
dst="$tmp/.build/x64/dist/sshfs-win-${version}-x64.msi"
status="$tmp/.build/x64/status/dist"
printf 'fixture-msi\n' > "$src"
touch "$tmp/.build/x64/status/wix"

# Legacy signing selectors must be opt-in, not inherited from the historical
# Navimatics/DigiCert packaging identity.
grep -Eq '^SigningIssuer[[:space:]]*\?=[[:space:]]*$' "$tmp/Makefile" || {
    echo "SigningIssuer must default to empty" >&2
    exit 10
}
grep -Eq '^SigningCrossCert[[:space:]]*\?=[[:space:]]*$' "$tmp/Makefile" || {
    echo "SigningCrossCert must default to empty" >&2
    exit 11
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

# A signed build also publishes only after the signing command succeeds. The
# default command must not inject a legacy cross-certificate or issuer filter.
rm -f "$dst" "$status" "$tmp/sign.args"
SIGN_RC=0 SIGN_ARGS="$tmp/sign.args" PATH="$tmp/mock/bin:$PATH" WIX="$tmp/mock" \
    make -C "$tmp" -f Makefile -o ".build/x64/status/wix" ".build/x64/status/dist" >/dev/null
[[ -f "$dst" && -f "$status" ]] || {
    echo "successful signing did not publish artifact" >&2
    exit 5
}
if grep -Fxq '/ac' "$tmp/sign.args" || grep -Fxq '/i' "$tmp/sign.args"; then
    echo "default signing unexpectedly used a legacy cross-certificate or issuer selector" >&2
    exit 6
fi

# Compatibility selectors remain available only when a maintainer explicitly
# requests them for a particular certificate setup.
rm -f "$dst" "$status" "$tmp/sign.args"
SIGN_RC=0 SIGN_ARGS="$tmp/sign.args" PATH="$tmp/mock/bin:$PATH" WIX="$tmp/mock" \
    make -C "$tmp" -f Makefile -o ".build/x64/status/wix" \
    SigningIssuer='"Example CA"' SigningCrossCert='"legacy.cer"' \
    ".build/x64/status/dist" >/dev/null
[[ $(grep -Fxc '/ac' "$tmp/sign.args") -eq 1 ]] || {
    echo "explicit SigningCrossCert did not enable /ac" >&2
    exit 7
}
[[ $(grep -Fxc '/i' "$tmp/sign.args") -eq 1 ]] || {
    echo "explicit SigningIssuer did not enable /i" >&2
    exit 8
}

echo "distribution signing contract: PASS"
