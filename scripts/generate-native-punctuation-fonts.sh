#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "$0")/.." && pwd)"
OUTPUT_DIR="native/fonts"
cd "$ROOT_DIR"
FONT_FILE="native/build/_deps/lvgl-src/scripts/built_in_font/Montserrat-Medium.ttf"

if [[ ! -f "$FONT_FILE" ]]; then
  echo "LVGL's pinned Montserrat source is missing; run npm run native once first." >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"
for size in 8 10 12 14 16 20; do
  npx --yes lv_font_conv@1.5.3 \
    --no-compress --no-prefilter --bpp 4 --size "$size" \
    --font "$FONT_FILE" \
    -r 0x2010-0x2026 -r 0x2190-0x2193 -r '0x7C=>0x2502' \
    --format lvgl \
    --lv-font-name "lv_font_cpzero_punct_${size}" \
    --lv-fallback "lv_font_montserrat_${size}" \
    -o "$OUTPUT_DIR/lv_font_cpzero_punct_${size}.c"
done
