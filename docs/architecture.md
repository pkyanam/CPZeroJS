# Architecture

CPZeroJS combines a TypeScript SDK, a Node.js-based build and development toolchain, and a native app host. Source TypeScript is bundled into a JavaScript IIFE. The host evaluates the bundle in QuickJS and uses LVGL 9 for widgets and layout. SDL presents the simulator on desktop. A Linux framebuffer backend supports the device build path; its framebuffer format, keyboard mapping, libcurl/TLS availability, and target image ABI remain unverified on physical Cardputer Zero hardware.

```text
TypeScript app ── esbuild ──> app.js ──> QuickJS ── bridge ──> LVGL ──> SDL simulator
                                  ▲          │                     ▲
                                  └── events ┘       input ────────┘
                                             native I/O worker
                                      process / HTTP / Codex CLI
```

The SDK creates and updates native widgets through the `__cp` bridge. Native widget identifiers increase monotonically, with `0` reserved for the screen parent. Native events are dispatched to JavaScript callbacks. The host also drains pending Promise jobs and invokes the SDK timer tick. JavaScript errors must be surfaced by the host.

The UI uses LVGL flex row and column layout. Its logical screen is always 320 × 170 pixels. Desktop scale affects the rendered window size, not the coordinate system the app uses.

The compact UI supports keyboard focus, text entry and paste, scrolling, and mouse input in the SDL simulator. Tab and Shift+Tab move focus; Enter activates a button or submits a single-line input; Shift+Enter inserts a newline in multiline input. Mouse clicks and wheel events pass through the native input path. Design for keyboard use first because the device has a small screen and physical keyboard. Simulator responsiveness is not a device performance measurement.

Long-running process and HTTP operations run on one native worker thread. A bounded completion queue returns events to the app pump; the worker never call QuickJS or LVGL. The Codex adapter runs the installed CLI app-server as a child process, with its own process memory and inherited CLI authentication/configuration. The app does not access credential files or API tokens.

## Development reload

`cpzero dev` rebuilds the app bundle when watched source changes. The native host's watch mode detects bundle replacement and reconstructs the JavaScript context and UI tree. This is a full app reload: signals, timers, and other in-memory state reset. Persist data using a service such as storage when it must survive a reload.

## Extension points

Apps can register JavaScript service adapters and call services by name. A service can implement app logic or adapt a native capability. The built-in storage service is backed by the host. Additional native widgets or device capabilities require matching native implementation; registering a JavaScript adapter alone does not create access to arbitrary hardware.

## Current limits

- App code has no JSX, browser DOM, or Node.js APIs in app code.
- The native widget set is deliberately small: screen, column, row, label, button, input, bar, box, and rich text spans. The core composes Markdown, disclosures, focus scopes, and theme recipes from these.
- v0.2 includes bounded asynchronous HTTP and process services. HTTP uses libcurl and the native build requires the corresponding dependency; device networking and TLS configuration remain unverified.
- Apps needing capabilities beyond the shipped widgets and services need custom service code; new native widgets require C/C++ work.
- The repository packages an externally built Linux ARM64 host; it includes a framebuffer backend whose behavior is not yet verified on the physical Zero. Device behavior is unverified.
- Permissions and the QuickJS memory cap do not constitute a security sandbox or a total process memory guarantee.

See the [API reference](api.md), [components](components.md), and [services](services.md) for current interfaces.
