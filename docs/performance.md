# Performance and resource use

CPZeroJS combines QuickJS app execution with LVGL widget creation and rendering. Cost depends on the operation and app. This project does not claim a blanket O(1) or O(N) runtime bound: rendering, layout, list-like work in app code, and native widget operations have different costs and should be measured in their actual context.

## Current runtime limits

The host applies a default 16 MiB JavaScript memory cap. LVGL/native allocations are tracked separately, and total process RSS is not guaranteed by that cap. The host drains Promise jobs while processing callbacks and applies an execution deadline to native calls. Confirm exact settings in the current native source and implementation contract before relying on them.

Some SDK costs are visible from the implementation: setting a signal notifies its current listeners, so notification work grows with listener count; each host timer tick scans registered timers to find due callbacks; disposing a widget tree visits its descendants. These are code-level bounds for those operations, not end-to-end latency guarantees. The native layout and rendering cost depends on LVGL work and tree shape.

`ui.pagedList` renders at most `floor(height / rowHeight)` rows for the current page. Moving pages disposes current rows and creates the next page's rows, so widget work depends on page size rather than total item count. Updating the item array does not make access to supplied data constant-time; cost depends on the array operation and row renderer.

`ui.pagedList` limits its rendered row widgets to `floor(height / rowHeight)` for the current page. Moving pages disposes the current page's rows and creates the next page's rows, so widget work depends on page size rather than the total number of items. Updating the item array does not make access to the supplied data constant-time; its cost depends on the array operation and row renderer.

## Practical guidance

- Keep the widget tree only as large as the screen needs; the display is 320 × 170 logical pixels.
- Avoid recreating widgets for high-frequency updates; update existing widgets when possible.
- Keep callbacks and timer work short so the single app process stays responsive.
- Choose timer intervals based on visible behavior rather than polling aggressively.
- Measure on both the simulator and the target device for timing-sensitive work. Desktop results do not establish device performance.

Run `npm run benchmark` after building the native host. It runs the dashboard headlessly and writes `.cpzero/benchmark/report.json`. On this development Mac (arm64, macOS/Darwin 25.6.0), the initial dashboard used 17 widgets and approximately 181 KB of QuickJS-managed allocation. The configured LVGL pool capacity is 2,097,152 bytes. Neither number represents total process memory.

A separate `/usr/bin/time -l native/build/cpzero-host dist/app.js --headless --frames 300` run reported 65,732,608 bytes maximum resident set size and 8,995,632 bytes peak memory footprint on this Mac. These OS metrics account for memory differently; the SDL simulator and system libraries contribute beyond the engine's allocations. They do not establish ARM64 Linux/device memory usage, startup time, display frame rate, or battery performance. `--frames` counts host loop iterations, not guaranteed display refreshes.

The project does not yet include a general profiler. Measure the framebuffer build on the actual target before setting application-wide memory budgets.
