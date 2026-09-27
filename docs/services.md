# Services

Services are CPZeroJS's extension point for app capabilities that do not belong in generic UI widgets. Register a local adapter with `services.register` and invoke it with `services.call`:

```ts
services.register("catalog", {
  async find(query: string) {
    return localCatalog.filter(item => item.name.includes(query));
  },
});

const matches = await services.call("catalog", "find", "clock");
```

The adapter object retains its concrete method types at registration. Calls use dynamic service and method names; pass a result type to `services.call<Result>` when you need to describe its return value. Calls use promises whether they reach a local adapter or the native host, and local methods preserve their `this` receiver.

`services.call(name, method, ...args)` first uses a registered JavaScript implementation when available, then falls back to the native service bridge. The native bridge exchanges JSON arguments and JSON results; errors are thrown. The exported `storage.get(key)` and `storage.set(key, value)` helpers call the built-in host storage service.

## Storage

In dev mode, the CLI uses the project’s `.cpzero/data` directory. Direct host execution persists app data under `CPZERO_DATA_DIR` when set, otherwise under `~/.local/share/cpzero/<safe-app-name>`. Keys are validated and values are bounded. Check the implementation contract and current source for exact limits before designing around a particular payload size. A missing key returns `null`; a stored JSON `null` has the same result. Storage persists data across app reloads; ordinary JavaScript variables do not.

## Adding a capability

For a pure app-level adapter, register a service in JavaScript and define its inputs and outputs for the app. For operating-system or hardware access, implement the native side too, expose a named native service, and register a JS adapter if it makes the app-facing API easier to use. Treat native service inputs as untrusted data and return clear errors for unsupported operations.

## Asynchronous process and HTTP services (v0.2)

The native host includes `process` and `http` services for bounded background work. Use the SDK clients instead of calling native services directly. Process launch takes an executable and an argument array; arguments are passed directly without shell interpolation. The child inherits the host environment and has bounded, nonblocking stdin/stdout/stderr pipes. Output is delivered in chunks and the exit event follows drained output. Close or kill a child when its work is finished, and always dispose listeners during app cleanup. This is process execution, not a sandbox: a child has the host user's permissions.

The HTTP service accepts HTTP and HTTPS URLs, verifies TLS, and limits request duration, response size, and completion-queue delivery. Cancel requests when the result is no longer needed. Treat response bodies and headers as untrusted input. These limits protect responsiveness and memory use; they do not provide a security boundary or guarantee internet access on a device.

Both services execute I/O on one native worker, outside the UI thread. The worker does not touch QuickJS or LVGL; the host transfers bounded completion events back to the SDK pump. Queue saturation is reported as an error/cancellation so a stream cannot be silently corrupted. A Promise wrapper around a synchronous native service would not move that service to a worker.

The bundled native host now requires libcurl for HTTP. The Linux ARM64/device host must be built with a compatible libcurl and TLS backend. macOS simulator behavior does not verify that device image dependency or network configuration.

## Codex integration

See [Codex app-server](codex.md) for the reusable adapter and demo. It starts the installed Codex CLI app-server as a separate process, so app-server memory and SDK QuickJS memory are separate. The adapter relies on the CLI's existing authenticated account/configuration; apps never read, store, or forward authentication tokens and do not need an API key. Codex permission requests remain visible decisions in the UI.

## Compile a native service module

See [device_info.cpp](../examples/native-service/device_info.cpp) for a complete example. Register a JSON service using `cpzero::registerService`, then compile it into the host:

```sh
npm run native -- -DCPZERO_EXTENSION_SOURCES="$PWD/examples/native-service/device_info.cpp"
```

App code calls `await services.call("deviceInfo", "profile")`. The host validates the returned JSON. Multiple extension source paths can be supplied as a quoted semicolon-separated CMake list.

Native handlers execute on the UI thread. Keep them short and nonblocking; wrapping a native call in a Promise does not move it to a worker. The built-in process and HTTP services are exceptions: their native implementations use a worker/completion queue. Custom extensions remain synchronous unless their implementation explicitly provides a safe asynchronous path. Native extensions are trusted code with process privileges.

HTTP responses are buffered text, not streaming downloads. Redirects are returned to the caller rather than followed automatically. Use `http.start(...).cancel()` to cancel a request; use process streams for structured subprocess protocols. See [resource limits](performance.md).
