# Codex app-server

`@cpzero/codex` connects a CPZero app to the installed Codex CLI app-server. The CLI runs as a child process with its existing account and configuration. Apps do not read or store credentials and do not need an API key.

## Client and events

Create a `CodexClient` with a core process `spawn` callback or an injected transport. Call `connect()`, then `newThread()` or `resume(id)`, then `send(text)`. Use `cancel()` to interrupt the active turn and `dispose()` from app cleanup. The `transcript` getter is a bounded compatibility tail; render `item` events by `itemId` when keeping separate messages.

`on(listener)` receives `delta` and normalized `item` events, along with generic `activity`, server requests (`approval`, `user-input`, `server-request`), `error`, and `closed` events. An item event has this shape:

```ts
{
  type: "item",
  phase: "started" | "completed" | "output" | "progress",
  itemId?: string,
  itemType: string,
  threadId?: string,
  turnId?: string,
  summary?: string,
  detail?: string,
  output?: string,
  status?: string,
  success?: boolean
}
```

Agent messages have distinct item IDs. Completed messages include their final text in `output`, even when the server sent no deltas. Command executions, file changes, MCP calls, web search, image generation, plan changes, and diffs include compact summaries and available detail. Command and file output chunks use the `output` phase. Normalized strings are capped at 4096 characters by default (`maxItemFieldChars`); the NDJSON line and compatibility transcript also have separate limits. Known methods remain available through `activity`, and unrecognized notifications are forwarded there for compatibility.

## Approval and user input

Approval requests are surfaced with their server `id`, `method`, and `params`. Nothing is accepted automatically. For command and file-change approval methods, call `resolveApproval(id, "accept")` or `resolveApproval(id, "decline", reason?)`. The client encodes the decision shape for the specific v2 or legacy request method. The demo starts threads with a read-only sandbox and on-request approvals.

User-input and other server requests need method-specific result data; answer them with `respond(id, result)`. `deny(id, message?)` returns a JSON-RPC error when a request should be rejected at the protocol level. `resolveApproval` intentionally does not handle permission-profile requests; those need a result matching that request's schema before an app can expose a safe decision.

## Process and protocol

The app-server uses newline-delimited JSON over stdio. It runs separately from QuickJS, inherits the host environment, and uses the host user's permissions. Select its working directory deliberately; the Codex demo uses `CPZERO_CODEX_CWD` or the current directory. The process service itself is not a sandbox.

The protocol subset and generated schema are tied to Codex CLI 0.157.1. Other versions can add or change methods; unknown notifications stay visible through `activity`, but the adapter does not claim support for every method. The [official App Server guide](https://learn.chatgpt.com/docs/app-server) describes its transport, initialization, threads, turns, events, and approvals.

## Demo

Run the chat demo from the repository root:

```sh
npm run dev:codex
```

The simulator uses a 320 × 170 logical-pixel display. Keyboard controls include Enter to send, Tab / Shift+Tab to move focus, Escape to stop or deny, Ctrl+N / Cmd+N for a new conversation, PageUp / PageDown to scroll, and `/` to focus the composer. Mouse users can click the same controls and use the wheel. Supported command/file-change approval requests offer Allow once and Deny; other request types are not silently accepted.

Starting a new conversation clears the demo's visible conversation and its in-memory transcript tail. It does not archive or delete the CLI's saved thread history. If a turn is starting or running, the demo first requests cancellation and waits for the turn ID before starting the next thread.

Codex CLI installation, sign-in, and compatible host subprocess support are prerequisites. Cardputer Zero/Linux behavior remains unverified until tested on hardware, including keyboard mapping, child process startup, memory, and performance. A responsive macOS simulator does not establish hardware behavior.

## Compact interaction

**New** clears all visible messages, tool cards, and pending dialogs immediately, then interrupts the old turn and starts a fresh thread. Late events from the previous thread are ignored. This clears the app's view and in-memory state; the CLI's own saved history is not deleted.

There are no on-screen zoom buttons. Use **Ctrl/Cmd + plus/minus** to resize text, and **Ctrl/Cmd + 0** to reset. The display remains 320 × 170 logical pixels. **Ctrl/Cmd + Up/Down** focuses the previous/next tool card; **Enter/Space** toggles it, **Left/Right** collapses/expands it, and **Up/Down** scrolls when the composer is not focused. **Alt + Up/Down** and **Page Up/Down** scroll from the composer too. **Escape** returns focus to the composer when idle. Expanded tool bodies are created lazily and freed on collapse.

Approval dialogs trap focus inside their controls. **Tab/Shift+Tab** switches controls; **Enter** activates the focused button; **Escape** denies the current request; arrows or Page Up/Down scroll its details. The model cannot approve its own requests.

The demo palette uses charcoal surfaces sampled from the desktop app, with rounded cards/buttons and a light primary action. Edit the semantic `theme` object near the top of `examples/codex/src/app.ts` to restyle it. That module exports `createCodexApp({ initialPrompt?, createClient? })` for a seeded demo or a custom backend adapter; `src/main.ts` starts the normal installed-CLI experience.
