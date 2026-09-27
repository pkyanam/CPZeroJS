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

Common props include `text`, `width`, `height`, `grow`, `gap`, `padding`, `bg`, `color`, `fontSize`, `hidden`, and `disabled`. Width and height accept pixel numbers or `"100%"`; supported font sizes may be snapped by the native font set. Value-oriented widgets also use `value`, `min`, and `max`. Button text is rendered by its internal label; label text belongs to the label itself.

Callbacks can be passed as `onPress` and `onChange` props or registered with `widget.on("press", callback)` / `widget.on("change", callback)`. Input change values are strings. `update` changes native properties; `dispose` removes the widget and its owned resources.

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

## Services

`services.register(name, implementation)` accepts an object whose methods retain their TypeScript types. `services.call<Result>(name, method, ...args)` uses dynamic string names and unknown arguments, then returns `Promise<Result>`; specify the result type when needed. It invokes a registered method when present, otherwise calls the native bridge and parses its JSON result. JavaScript adapter methods retain their `this` receiver.

## Bridge and exact details

Native bridge operations and supported properties are documented in the [implementation contract](implementation-contract.md). Use `services.call` for capabilities instead of assuming Node or browser globals. If an API is missing from the shipped package, check source and the contract before treating it as available.
