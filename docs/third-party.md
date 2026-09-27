# Third-party components

CPZeroJS source is MIT licensed. Its native build fetches these pinned upstream components; they keep their own licenses and notices:

| Component | Pinned version | License | Role |
| --- | --- | --- | --- |
| [LVGL](https://github.com/lvgl/lvgl/tree/v9.2.2) | v9.2.2 | MIT | Layout, widgets and software rendering |
| [QuickJS-ng](https://github.com/quickjs-ng/quickjs/tree/v0.9.0) | v0.9.0 | MIT | Embedded JavaScript engine |
| [libcurl](https://curl.se/libcurl/) | System-provided | curl license | HTTP(S), TLS and connection handling |
| [SDL](https://github.com/libsdl-org/SDL) | System-provided SDL2-compatible package | zlib | Desktop window and input |

See `native/CMakeLists.txt` for exact revisions and archive checksums. Build dependencies are downloaded into the ignored build directory rather than copied into the repository. Copies of the LVGL, QuickJS-ng, Montserrat and Font Awesome notices are in `third_party/` and included by the device package helper. LVGL's built-in Montserrat font also incorporates Font Awesome glyphs. Retain these notices when distributing native binaries.

Development tools (TypeScript, esbuild, tsx, Node.js) have their own licenses. `package-lock.json` records the npm dependency versions. They are development-side tools, not the app's JavaScript runtime.

[PocketJS](pocketjs-research.md) informed the design review. No PocketJS source code is included in this release.
