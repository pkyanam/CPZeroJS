#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${CPZERO_BUILD_DIR:-$ROOT/native/build}"
BACKEND="${CPZERO_BACKEND:-SDL}"
cmake -S "$ROOT/native" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCPZERO_BACKEND="$BACKEND" "$@"
cmake --build "$BUILD_DIR" --parallel
printf 'Built %s\n' "$BUILD_DIR/cpzero-host"
