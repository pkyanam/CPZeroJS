# CPZero punctuation fallback fonts

These small LVGL fonts add Montserrat glyphs U+2010–U+2026 and U+2190–U+2193,
plus a U+2502 box-drawing pipe mapped from Montserrat's ASCII vertical bar.
Each font falls back to the matching LVGL built-in Montserrat size, so ordinary
Latin text continues to use the existing built-in font tables.

Regenerate them with `bash scripts/generate-native-punctuation-fonts.sh` after
the pinned LVGL source has been fetched by `npm run native`. The script uses
`lv_font_conv` 1.5.3, 4 bpp, and the bundled
`Montserrat-Medium.ttf`. `OFL.txt` contains its SIL Open Font License 1.1.
