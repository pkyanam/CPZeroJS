# Agent guide

CPZeroJS is an early TypeScript-to-JavaScript SDK and native app host. Before changing behavior, read [the implementation contract](docs/implementation-contract.md) and the most relevant guide below. Keep docs aligned with code and label planned or unverified behavior clearly.

## Quick map

- `packages/core`: app lifecycle, widget wrappers, signals, timers, services.
- `packages/cli`: `cpzero` commands, app bundling, scaffolding, and dev watch loop.
- `native`: C++ host, QuickJS bridge, LVGL widgets, and SDL desktop host. Device packaging expects a separately built ARM64 host.
- `examples/dashboard`: runnable example app.
- `docs`: user guides, API notes, and the implementation contract.

Exact paths may evolve; check `rg --files` when locating a component.

## Contracts to preserve

- The app surface is 320 × 170 logical pixels. Simulator scaling changes the window size, not logical dimensions.
- App code is bundled TypeScript/JavaScript without Node or browser globals. Build/development tooling uses Node.js.
- Native widget IDs are monotonic. ID `0` identifies the screen parent.
- A watched bundle reload reconstructs the JavaScript context and UI; state resets by design.
- Use `services` for app/platform capabilities. Do not imply that arbitrary hardware, network, or operating-system APIs are present.
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
