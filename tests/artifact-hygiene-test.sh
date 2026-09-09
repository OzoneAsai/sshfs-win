#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

bad=0
while IFS= read -r path; do
    [[ -z "$path" ]] && continue
    echo "generated residue in source artifact: ${path#$ROOT/}" >&2
    bad=1
done < <(find "$ROOT" \
    \( -name .codegraph -o -name .build -o -name '__pycache__' -o -name '.cp31g-test2' \) -print -prune -o \
    -type f \( -name '*.o' -o -name '*.out' -o -name '*.log' -o -name '*.pyc' -o -name '*.tmp' -o -name '*.bak' -o -name '*~' \) -print)

(( bad == 0 )) || exit 1

echo 'artifact hygiene contract: PASS'
