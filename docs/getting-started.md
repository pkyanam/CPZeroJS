# Getting started

CPZeroJS apps are TypeScript projects bundled for a small embedded JavaScript runtime. The SDK supplies app lifecycle, widgets, reactive signals, timers, and a service extension point. The native host uses QuickJS and LVGL.

## Run the repository example

Use Node.js 22 or newer and npm. On macOS, install the desktop build tools:

```sh
brew install cmake ninja sdl2 pkg-config
```

Then from the repository root:

```sh
npm install
npm run native
npm run dev
```

The first native build fetches pinned LVGL and QuickJS sources through CMake, so it needs network access.

The v0.2 HTTP host also links libcurl. Install the package with headers on macOS (`brew install curl`) if CMake cannot find it. Codex demo use additionally requires the Codex CLI to be installed and signed in on the same machine; the demo starts that CLI and uses its existing account/configuration. No API key is needed by the app.

The default app is `examples/dashboard/src/main.ts`. The SDL simulator has a fixed 320 × 170 logical display at 3× scale by default. The scale changes presentation size, not app coordinates.

During `cpzero dev`, source changes trigger a bundle rebuild and host reload. Reloading replaces the JavaScript context and UI, so in-memory app state starts over. Save durable user data through the storage service instead.

## Create an app beside this checkout

The current scaffold uses local `@cpzero/core` and `@cpzero/cli` packages from this repository. From the repository root:

```sh
node packages/cli/src/index.mjs init ../my-app
cd ../my-app
npm install
npm run dev
```

The scaffold has its own package manifest and `src/main.ts`. Keep this checkout available: the generated project refers to its local packages and its `dev` command uses the native host built here. If needed, set `CPZERO_HOST` to the host executable's full path. The CLI also supports:

```sh
cpzero dev [entry]
cpzero build [entry]
cpzero doctor
```

`dev` bundles and watches the app while launching the native host. Successful rebuilds replace the bundle atomically; a failed build leaves the previous bundle running. The host reloads the new bundle by rebuilding the JS context and UI, resetting in-memory state. `build` writes the bundled app and manifest under `dist/`. Available simulator options are listed in [Device and simulator](device.md#simulator-options).

## First app

```ts
import { createApp, ui, signal, bind } from "@cpzero/core";

createApp({ title: "Hello", setup(root) {
  const count = signal(0);
  const page = ui.column(root, { padding: 8, gap: 8 });
  const label = ui.label(page, { text: "0" });
  bind(label, "text", count);
  ui.button(page, { text: "Count", onPress: () => count.value++ });
}});
```

The `root` provided to `setup` is the app screen. See the [API reference](api.md) for supported widgets and properties. The runtime has no JSX, Node.js APIs, or browser DOM.

The SDL simulator supports keyboard and mouse input. Use Tab/Shift+Tab to move focus, Enter to activate a focused button, type into a focused input, and use the wheel or keyboard navigation to scroll. Mouse users can click controls and scroll normally. Build compact screens for keyboard operation first; simulator behavior does not prove Linux device input works.
