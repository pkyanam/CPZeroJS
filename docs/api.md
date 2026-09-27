# API reference

The public API currently lives in `@cpzero/core`. App bundles run in QuickJS, so examples use standard JavaScript features but cannot rely on Node.js or browser globals.

## App lifecycle

```ts
createApp({ title?: string, setup(root: Widget): void }): App
```

`setup` receives the screen widget. Register app UI and callbacks there. `onCleanup(callback)` registers lifecycle cleanup. The `App` returned by `createApp` has a `dispose()` method that releases its widgets, bindings, timers, and cleanup callbacks.

## Widgets

Create widgets with `ui.column(parent, props)`, `ui.row(parent, props)`, `ui.label(parent, props)`, `ui.button(parent, props)`, `ui.input(parent, props)`, `ui.bar(parent, props)`, and `ui.box(parent, props)`. `ui.custom(parent, kind, props)` creates an extension kind, but rendering requires native support for that kind. Each returns a widget with `id`, `update(props)`, `dispose()`, and `on(event, callback)`.

`ui.pagedList(parent, options)` creates a paged list and renders rows for only the current page. Required options are `items`, `height`, `rowHeight`, and `renderRow(item, index, row)`; `width` and `gap` are optional. The returned `PagedList` exposes `widget`, `pageSize`, `page`, `pageCount`, `isDisposed`, `setItems(items)`, `goToPage(index)`, and `dispose()`. `page` is zero-based; `pageSize` is `floor(height / rowHeight)` and the viewport must fit at least one row. An empty list displays `0 / 0`.

Common props include `text`, `width`, `height`, `grow`, `gap`, `padding`, `bg`, `color`, `fontSize`, `hidden`, and `disabled`. In v0.2 compact layouts also support content sizing, min/max dimensions, separate horizontal/vertical padding, borders, radius, focusColor, opacity, alignment, text alignment, wrapping/ellipsis/clipping, scrolling, and focusability. Inputs add placeholder, multiline, maxLength, and lineSpacing. Width and height accept pixels, `"content"`, or percentages; supported font sizes are 8, 10, 12, 14, 16, and 20 pixels, subject to the native font set. Value-oriented widgets also use `value`, `min`, and `max`.

Callbacks can be passed as `onPress` and `onChange` props or registered with `widget.on(...)`. In v0.2 widgets also emit `submit`, `focus`, `blur`, and `scroll`; input values are strings. Single-line Enter submits; Shift+Enter inserts a newline in multiline input. `update` changes native properties; `dispose` removes the widget and its owned resources. Keyboard users can Tab/Shift+Tab between focusable controls. Buttons show a focus indicator. On desktop, mouse clicks and wheel scrolling use the same native event path.

## Signals and bindings

```ts
const count = signal(0);
count.value = count.value + 1;
const unsubscribe = count.subscribe(value => { /* react to value */ });
const unbind = bind(label, "text", count);
```

`signal(initial)` returns a value getter/setter and `subscribe`. `bind(widget, property, signal)` keeps a widget property synchronized and associates cleanup with the widget. Call an unsubscribe function when a subscription should end earlier.

## Timers

`every(milliseconds, callback)` repeats a callback and `after(milliseconds, callback)` schedules it once. Both return cleanup functions. Timer callbacks run when the host calls the SDK tick; they are not independent operating-system threads.

`storage.get<T>(key)` returns `Promise<T | null>`; missing keys return `null`. `storage.set(key, value)` returns `Promise<void>` and accepts JSON-serializable data. Both use the built-in persistent host storage service. `log(message)` writes to the host log and `stats()` returns host runtime statistics.

Widgets expose `focus()`, `scrollTo(y)`, `scrollBy(y)`, `scrollToEnd()`, `getValue()`, `getScrollY()`, and `isFocused()` for programmatic focus, scrolling, and input/viewport reads. See [recipes](recipes.md) for keyboard-first patterns and the current implementation contract for native bridge details.

`keyboard.on(handler)` registers an app-wide shortcut handler receiving `{ key, ctrl, shift, alt, meta }`. Return `true` to consume the key; returning `false` or nothing lets normal native widget handling continue. The returned cleanup function unregisters the handler, and app disposal clears remaining handlers.

## Services

`services.register(name, implementation)` accepts an object whose methods retain their TypeScript types. `services.call<Result>(name, method, ...args)` uses dynamic string names and unknown arguments, then returns `Promise<Result>`; specify the result type when needed. It invokes a registered method when present, otherwise calls the native bridge and parses its JSON result. JavaScript adapter methods retain their `this` receiver.

v0.2 adds asynchronous native process, HTTP, and Codex clients. The process client launches an executable with an argument array (no shell parsing); HTTP accepts HTTP(S) and validates TLS. Their work runs on native workers and the UI thread polls bounded completion events. Check [services](services.md) for their limits and [Codex](codex.md) for authentication and approval behavior.

## Bridge and exact details

Native bridge operations and supported properties are documented in the [implementation contract](implementation-contract.md). Use `services.call` for capabilities instead of assuming Node or browser globals. If an API is missing from the shipped package, check source and the contract before treating it as available.

### Native async I/O

`processes.spawn({ command, args?, cwd? })` returns a `Process`. It exposes `write(text)`, `closeStdin()`, `kill()`, `dispose()`, and `on("stdout" | "stderr" | "error", callback)` / `on("exit", callback)`; exit callbacks receive a number and the others a string. This launches an executable directly with an argument array and never invokes a shell. Dispose it during app cleanup.

`http.start(options)` returns `{ promise, cancel }`, where the promise resolves to `{ status, ok, headers, body, json<T>() }`. Options include `url`, optional method, headers, body, timeoutMs, and maxBytes. Only HTTP(S) is accepted. `http.request(options)` returns only the promise; use `start` when explicit cancellation is needed. Host workers keep network and process I/O off the UI thread, but result processing and callbacks still run in the app pump.

### Codex adapter

`@cpzero/codex` exports `CodexClient` and `connectCodex`. A client can be constructed with an injected `transport` or `spawn` function for testing. Call `connect()` before starting/resuming a thread, then `send(text)`; call `cancel()` to interrupt an active turn. `on(listener)` receives bounded stream events including `delta`, `activity`, `approval`, `user-input`, `server-request`, `error`, and `closed`. `respond(id, result)` and `deny(id, message?)` answer server-initiated requests. The adapter's implemented protocol subset is tied to Codex CLI 0.157.1; see [Codex app-server](codex.md).

## Rich text, Markdown and zoom

`ui.richText(parent, { spans: [{ text: 'Hello ', color: '#ffffff' }, { text: 'world', fontSize: 14 }] })` wraps styled native spans. Use `markdown(parent, { text, fontSize: 12 })` for bounded Markdown parsing and widget reuse; the returned view supports `setText`, `setFontSize` and `dispose`. See [Markdown](markdown.md).

`createZoom({ levels: [8, 10, 12, 14, 16], initial: 12 })` exposes `value`, `subscribe`, `zoomIn`, `zoomOut`, `reset`, `set`, `canZoomIn` and `canZoomOut`. Bind it to your text views; it changes content size without changing display resolution. Updates clamp to the available levels. Own subscriptions with `widget.own(unsubscribe)`. See [theming](theming.md) for semantic styles and component recipes.
