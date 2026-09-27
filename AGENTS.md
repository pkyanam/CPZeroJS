# Agent guide

CPZeroJS is an early TypeScript-to-JavaScript SDK and native app host. Before changing behavior, read [the API reference](docs/api.md) and the most relevant guide below. Keep docs aligned with code and label planned or unverified behavior clearly.

## Quick map

- `packages/core`: app lifecycle, compact widgets, signals, timers, async service clients.
- `packages/codex`: typed adapter for the installed Codex CLI app-server protocol.
- `packages/cli`: `cpzero` commands, app bundling, scaffolding, and dev watch loop.
- `native`: C++ host, QuickJS bridge, LVGL widgets, and SDL desktop host. Device packaging expects a separately built ARM64 host.
- `examples/dashboard`: basic runnable app.
- `examples/codex`: live CLI chat frontend; `app.ts` exports its reusable app factory.
- `docs`: user guides, API notes, and the implementation contract.

Exact paths may evolve; check `rg --files` when locating a component.

## Contracts to preserve

- The app surface is 320 × 170 logical pixels. Simulator scaling changes the window size, not logical dimensions.
- App code is bundled TypeScript/JavaScript without Node or browser globals. Build/development tooling uses Node.js.
- Native widget IDs are monotonic. ID `0` identifies the screen parent.
- A watched bundle reload reconstructs the JavaScript context and UI; state resets by design.
- Use `services` for app/platform capabilities. The v0.2 native host includes bounded asynchronous process and HTTP services; process spawning executes a selected executable with the host user’s permissions; it is not a sandbox.
- Keep process/network work off the UI thread. Workers must not call QuickJS or LVGL; completion events return through the host pump.
- Codex integration uses the installed authenticated CLI as a child process. Never read or persist its credentials or claim that an API key is required.
- Make compact UI controls keyboard and mouse accessible. On Mac simulator test Tab/Shift+Tab, Enter, text entry/paste and scrolling as well as mouse; favor keyboard operation for the device.
- Report performance based on measurements. Do not claim every operation is O(1) or O(N) without specifying and verifying the operation.
- Label Cardputer Zero hardware behavior unverified until tested on the device.

## Useful commands

```sh
npm install
npm run native
npm run dev
node packages/cli/src/index.mjs doctor
```

For detailed setup and command behavior, see [Getting started](docs/getting-started.md) and [Contributing](docs/contributing.md).
