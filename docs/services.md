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

There is no built-in network adapter in v0.1, and a JS service does not create native network access by itself. Likewise, device peripherals require an implemented native service and device verification. The host's permissions are not a security boundary.

## Compile a native service module

See [device_info.cpp](../examples/native-service/device_info.cpp) for a complete example. Register a JSON service using `cpzero::registerService`, then compile it into the host:

```sh
npm run native -- -DCPZERO_EXTENSION_SOURCES="$PWD/examples/native-service/device_info.cpp"
```

App code calls `await services.call("deviceInfo", "profile")`. The host validates the returned JSON. Multiple extension source paths can be supplied as a quoted semicolon-separated CMake list.

Native handlers execute on the UI thread. Keep them short and nonblocking; wrapping a native call in a Promise does not move it to a worker. A worker/completion queue for long-running network or device I/O is a future runtime extension. Native extensions are trusted code with process privileges.
