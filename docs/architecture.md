# Architecture

CPZeroJS combines a TypeScript SDK, a Node.js-based build and development toolchain, and a native app host. Source TypeScript is bundled into a JavaScript IIFE. The host evaluates the bundle in QuickJS and uses LVGL 9 for widgets and layout. SDL presents the simulator on desktop. A Linux framebuffer backend supports the device build path; its framebuffer format, keyboard mapping, and target image ABI remain unverified on physical Cardputer Zero hardware.

```text
TypeScript app ── esbuild ──> app.js ──> QuickJS ── bridge ──> LVGL ──> SDL simulator
                                  ▲          │
                                  └── events ┘
```

The SDK creates and updates native widgets through the `__cp` bridge. Native widget identifiers increase monotonically, with `0` reserved for the screen parent. Native events are dispatched to JavaScript callbacks. The host also drains pending Promise jobs and invokes the SDK timer tick. JavaScript errors must be surfaced by the host.

The UI uses LVGL flex row and column layout. Its logical screen is always 320 × 170 pixels. Desktop scale affects the rendered window size, not the coordinate system the app uses.

## Development reload

`cpzero dev` rebuilds the app bundle when watched source changes. The native host's watch mode detects bundle replacement and reconstructs the JavaScript context and UI tree. This is a full app reload: signals, timers, and other in-memory state reset. Persist data using a service such as storage when it must survive a reload.

## Extension points

Apps can register JavaScript service adapters and call services by name. A service can implement app logic or adapt a native capability. The built-in storage service is backed by the host. Additional native widgets or device capabilities require matching native implementation; registering a JavaScript adapter alone does not create access to arbitrary hardware.

## Current limits

- v0.1 has no JSX, browser DOM, or Node.js APIs in app code.
- The native widget set is deliberately small: screen, column, row, label, button, input, bar, and box.
- No native network adapter is included. Apps needing network access must supply an adapter and corresponding platform support.
- Apps needing capabilities beyond the shipped widgets and services need custom service code; new native widgets require C/C++ work.
- The repository packages an externally built Linux ARM64 host; it does not currently implement or verify a Cardputer Zero framebuffer runtime. Device behavior is unverified.
- Permissions and the QuickJS memory cap do not constitute a security sandbox or a total process memory guarantee.

See the [implementation contract](implementation-contract.md) for bridge signatures, props, lifecycle, host flags, and storage behavior.
