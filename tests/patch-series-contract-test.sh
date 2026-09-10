#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SERIES="$ROOT/patches/SERIES"
[[ -f "$SERIES" ]] || { echo 'missing patches/SERIES' >&2; exit 1; }

declare -A seen=()
listed=0
while IFS= read -r name || [[ -n "$name" ]]; do
    name=${name%$'\r'}
    [[ -z "$name" || "$name" == \#* ]] && continue
    [[ "$name" != */* && "$name" == *.patch ]] || { echo "invalid SERIES entry: $name" >&2; exit 1; }
    [[ -z "${seen[$name]+x}" ]] || { echo "duplicate SERIES entry: $name" >&2; exit 1; }
    [[ -f "$ROOT/patches/$name" ]] || { echo "missing SERIES patch: $name" >&2; exit 1; }
    seen[$name]=1
    listed=$((listed + 1))
done < "$SERIES"

actual=0
for path in "$ROOT"/patches/*.patch; do
    name=${path##*/}
    actual=$((actual + 1))
    [[ -n "${seen[$name]+x}" ]] || { echo "unlisted patch: $name" >&2; exit 1; }
done
[[ $listed -eq $actual ]] || { echo "SERIES count mismatch listed=$listed actual=$actual" >&2; exit 1; }

# Consumers must use the manifest rather than implicit glob ordering.
if grep -Fq 'for f in $(PrjDir)/patches/*.patch' "$ROOT/Makefile"; then
    echo 'Makefile still relies on patch glob order' >&2; exit 1
fi
grep -Fq 'patches/SERIES' "$ROOT/Makefile"
grep -Fq 'GIT_CEILING_DIRECTORIES="$(PrjDir)" git apply' "$ROOT/Makefile"
grep -Fq 'patches/SERIES' "$ROOT/tools/verify-sshfs-source.sh"
grep -Fq 'patches/SERIES' "$ROOT/tools/apply-checkpoint.sh"

echo "patch series contract: PASS ($listed patches)"
