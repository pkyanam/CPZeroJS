# Compact UI and service recipes

## Keyboard-first controls

Make every action reachable with Tab and Shift+Tab, give buttons short labels, and keep the focus indicator visible against every background. Put the primary action after its input in focus order. Enter submits a single-line input; Shift+Enter adds a line to a multiline input. Use a mouse click and wheel in the SDL simulator too, so both input paths get exercised.

```ts
const prompt = ui.input(panel, {
  placeholder: "Ask Codex…",
  width: "100%",
  focusable: true,
});
const send = ui.button(panel, {
  text: "Send",
  focusable: true,
  onPress: () => submit(prompt.getValue()),
});
prompt.on("submit", () => submit(prompt.getValue()));
```

This sketch shows the interaction pattern; confirm property/event availability in the installed `@cpzero/core` types. Prefer a native submit event over wiring both Enter and button press to duplicated logic.

## Keep long work off the UI thread

Use the `processes` and `http` SDK clients for long-running work. Register output/error/exit listeners before sending input, render output in bounded chunks, and dispose or cancel work from `onCleanup`. Do not build a shell command string: pass the executable and each argument separately. Do not block the UI while waiting for a process or response. Native custom services are synchronous unless their implementation explicitly adds a worker path.

```ts
const child = processes.spawn({ command: "some-tool", args: ["--version"] });
child.on("stdout", chunk => appendBounded(chunk));
child.on("stderr", chunk => showError(chunk));
onCleanup(() => child.dispose());
```

`processes.spawn()` returns a `Process` with `write`, `closeStdin`, `kill`, `on`, and `dispose`. `http.start()` returns `{ promise, cancel }`; `http.request()` is the promise-only convenience. See [API reference](api.md) and [services](services.md).

## Scroll a transcript

Give a transcript a fixed viewport and vertical scrolling. Append only the newest bounded text, then scroll to the end when the user is already following the latest output. Keep manual scroll position when the user has moved away from the end. Avoid rebuilding an unbounded widget tree for every streamed chunk.

## Test input on Mac

Run the SDL simulator and verify the same task with keyboard and mouse: Tab through controls, Shift+Tab backwards, activate a button with Enter, type and paste Unicode text, submit a single-line field, add a multiline newline with Shift+Enter, scroll with the keyboard and wheel, and click controls. Confirm focus remains visible and actions do not require a mouse. These are interaction checks, not evidence of Cardputer Zero hardware performance.

## Connect the Codex adapter

The client process transport matches the core process API. Keep the client in app state, subscribe to events before connecting, and display each permission request with an explicit choice. Map the choices required by that particular server method into its response shape; do not guess a result payload or approve unknown request types.

```ts
import { processes, onCleanup } from "@cpzero/core";
import { CodexClient } from "@cpzero/codex";

const client = new CodexClient({
  spawn: options => processes.spawn(options),
});
const unsubscribe = client.on(event => {
  if (event.type === "delta") appendTranscript(event.text);
  if (event.type === "approval" || event.type === "user-input" || event.type === "server-request") {
    showDecision(event); // The user's choice calls client.respond(...) or client.deny(...).
  }
});
onCleanup(() => { unsubscribe(); client.dispose(); });
await client.connect();
await client.newThread();
```

This example uses the exact current exports. Read [Codex app-server](codex.md) before enabling anything beyond the demo's read-only sandbox and on-request approvals.
