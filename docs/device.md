# Device packaging and simulator

The app's logical display is 320 × 170 pixels. The SDL desktop simulator uses a default 3× presentation scale. Simulator scaling affects window size only; app layout remains in logical pixels.

## Simulator options

The native host accepts:

```text
<bundle.js> [--watch] [--scale N] [--headless] [--frames N] [--screenshot path.ppm] [--keys Enter,Tab,...]
```

Headless mode and PPM screenshots support native-render checks. Input key names and supported options can evolve; check `cpzero --help` for development options and the native host implementation for lower-level flags.

## Debian package helper

The repository includes `scripts/package-device.sh <app-id> [version]`. It does not cross-compile the host. It packages an already-built Linux ARM64 host plus `dist/app.js` as an `arm64` Debian package. Run `npm run build` to create the app bundle first. The helper requires Debian Linux, `dpkg-deb`, `readelf`, and a `CPZERO_HOST` pointing to an ARM64 Linux ELF host. `CPZERO_BUNDLE` can override the default `dist/app.js` path. On macOS, the script exits with an explanation because a Mac host binary cannot run on the Zero.

The helper places the executable and app bundle in the package, adds a launcher and APPLaunch desktop entry, and sets the app data directory. It checks the binary architecture, but does not verify the target OS ABI, framebuffer/display integration, keyboard permissions, launcher integration, or app behavior. Device behavior is currently unverified. The simulator does not emulate the operating system or hardware peripherals.

## Build the Linux framebuffer host

On the target-compatible Debian ARM64 build machine, with a C/C++ compiler and CMake installed:

```sh
cmake -S native -B native/build-fbdev -DCPZERO_BACKEND=FBDEV -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-fbdev --parallel 2
CPZERO_HOST="$PWD/native/build-fbdev/cpzero-host" npm run package:device -- my-app 0.1.0
```

This backend uses Linux framebuffer/evdev directly and does not require SDL. It accepts a 320 × 170 truecolor 16- or 32-bit framebuffer and respects its row stride and color channel layout. Set `CPZERO_FRAMEBUFFER` (default `/dev/fb0`) and `CPZERO_INPUT_DEVICE` (default keyboard autodetection) for the target image. The current keyboard map uses standard Linux evdev key codes; Zero-specific Fn/Sym mappings need hardware verification. Native dependencies must match the target image's C/C++ ABI; compiling on a newer Linux distribution is not a compatibility guarantee.

For a cross build, provide a CMake toolchain and target sysroot. This repository does not yet ship a validated M5Stack sysroot/toolchain preset.
