#!/usr/bin/env bash
set -euo pipefail
cd "$1"
for p in "$2"/*.patch; do
    echo "applying $p"
    patch -l -p1 < "$p"
done
