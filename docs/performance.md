# Performance and resource use

CPZeroJS combines QuickJS app execution with LVGL widget creation and rendering. Cost depends on the operation and app. This project does not claim a blanket O(1) or O(N) runtime bound: rendering, layout, list-like work in app code, and native widget operations have different costs and should be measured in their actual context.

## Current runtime limits

The host applies a default 16 MiB JavaScript memory cap. LVGL/native allocations are tracked separately, and total process RSS is not guaranteed by that cap. The host drains Promise jobs while processing callbacks and applies an execution deadline to native calls. Confirm exact settings in the current native source and implementation contract before relying on them.

v0.2 process and HTTP I/O runs on one native worker, with one bounded completion batch delivered through the app pump. This keeps waiting on a child or network response off the UI thread; it does not make app callbacks, parsing, text rendering, or layout free. The Codex app-server is a separate child process, so its memory is outside the QuickJS heap cap and total RSS can be substantially higher. Keep transcript/history buffers bounded and keep each event callback short.

Some SDK costs are visible from the implementation: setting a signal notifies its current listeners, so notification work grows with listener count; each host timer tick scans registered timers to find due callbacks; disposing a widget tree visits its descendants. These are code-level bounds for those operations, not end-to-end latency guarantees. The native layout and rendering cost depends on LVGL work and tree shape.

`ui.pagedList` renders at most `floor(height / rowHeight)` rows for the current page. Moving pages disposes current rows and creates the next page's rows, so widget work depends on page size rather than total item count. Updating the item array does not make access to supplied data constant-time; cost depends on the array operation and row renderer.

## Practical guidance

- Keep the widget tree only as large as the screen needs; the display is 320 × 170 logical pixels.
- Avoid recreating widgets for high-frequency updates; update existing widgets when possible.
- Keep callbacks and timer work short so the single app process stays responsive.
- Choose timer intervals based on visible behavior rather than polling aggressively.
- Update existing widgets for streamed output; avoid recreating an ever-growing transcript tree on each chunk.
- Make keyboard navigation and mouse operation both work in the SDL simulator. Responsive interaction is an observed simulator result, not a device frame-time guarantee.
- Measure on both the simulator and the target device for timing-sensitive work. Desktop results do not establish device performance.

Run `npm run benchmark` after building the native host. It runs the dashboard headlessly and writes `.cpzero/benchmark/report.json`. On this development Mac (arm64, macOS/Darwin 25.6.0), the initial dashboard used 17 widgets and approximately 181 KB of QuickJS-managed allocation. The configured LVGL pool capacity is 2,097,152 bytes. Neither number represents total process memory.

A separate `/usr/bin/time -l native/build/cpzero-host dist/app.js --headless --frames 300` run reported 65,732,608 bytes maximum resident set size and 8,995,632 bytes peak memory footprint on this Mac. These OS metrics account for memory differently; the SDL simulator and system libraries contribute beyond the engine's allocations. They do not establish ARM64 Linux/device memory usage, startup time, display frame rate, or battery performance. `--frames` counts host loop iterations, not guaranteed display refreshes.

The project does not yet include a general profiler. Measure the framebuffer build on the actual target before setting application-wide memory budgets.

## Text, themes and I/O budgets

Markdown retains at most 16,384 source characters and renders at most 32 blocks / 256 native spans per view; apps can lower those caps. The Codex demo uses 6,000 characters, 16 blocks and 96 spans per message, with at most 24 transcript rows. Streaming text updates are coalesced to one rendering pass every 33 ms. Unchanged Markdown blocks skip native updates. A changed block is parsed and laid out in proportion to its bounded contents; there is no claim of constant-time text layout.

Theme recipes create plain native props. Static themes need no subscriptions; scoped themes update only their subscribers and detach when widgets are disposed. Zoom chooses one of a small set of native font sizes and reflows text within the unchanged 320 × 170 display.

Native I/O allows up to 16 processes and 8 HTTP requests. Stdin buffering is 64 KiB per process; HTTP response bodies default to 256 KiB with a 512 KiB hard maximum. Response headers are capped at 32 KiB. The completion queue is limited to 256 events / 1 MiB and drains bounded batches. Overflow terminates outstanding work with an explicit error rather than silently dropping protocol data. Restart the app after a terminal I/O overflow.

The renderer presents once per completed LVGL refresh rather than waiting for display sync on each partial draw buffer. This avoids multiple vsync waits per frame; it is not a measured guarantee of 60 FPS on the Zero.

A v0.2 headless Codex demo after a real short reply used 20 widgets and approximately 379 KB of QuickJS-managed allocation on this Mac, with the same 2 MiB LVGL pool capacity. This excludes the separate Codex CLI process and native/system allocations; it is not the combined app memory footprint.
