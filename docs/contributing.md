# Contributing

CPZeroJS is an early SDK and native host. Changes should preserve a clear separation between app code, development tooling, the JavaScript bridge, and platform-specific native behavior.

## Repository map

- `packages/core`: TypeScript app SDK.
- `packages/cli`: CLI, bundling, watch/reload, and scaffold.
- `native`: C++ host, QuickJS integration, LVGL UI, SDL simulator. Device packaging expects a separately built ARM64 Linux host.
- `examples/dashboard`: reference app.
- `docs`: user guides and the implementation contract.

Paths may move as the codebase grows; use `rg --files` to locate current files.

## Local workflow

```sh
npm install
npm run native
npm run dev
```

Keep public API documentation consistent with the SDK exports and the implementation contract. For a new capability, identify whether it belongs in the SDK, a JavaScript service adapter, or a native service/widget. Document unavailable platform support honestly. Treat hardware verification as a separate step from a successful cross-build.

When reporting performance, specify the operation and measured environment. Do not describe all runtime paths as O(1) or O(N) without evidence. Changes to reload behavior should explain whether app state resets and how persistent state is handled.

The CLI's `doctor` command can report development prerequisites. Consult package scripts and command help for the current verification commands; this project is evolving and tooling may change.
