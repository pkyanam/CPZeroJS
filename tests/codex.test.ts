import test from "node:test";
import assert from "node:assert/strict";
import { CodexClient, type CodexTransport } from "../packages/codex/src/index.js";
import type { NativeBridge } from "../packages/core/src/index.js";

class MockTransport implements CodexTransport {
  writes: string[] = [];
  handlers = new Map<string, Set<(value: string | number) => void>>();
  killed = false;
  spawnedOptions?: { command: string; args?: string[]; cwd?: string };
  write(text: string): void { this.writes.push(text); }
  closeStdin(): void {}
  kill(): void { this.killed = true; }
  dispose(): void {}
  on(event: "exit", callback: (value: number) => void): () => void;
  on(event: "stdout" | "stderr" | "error", callback: (value: string) => void): () => void;
  on(event: "stdout" | "stderr" | "exit" | "error", callback: (value: any) => void): () => void {
    let callbacks = this.handlers.get(event);
    if (!callbacks) this.handlers.set(event, callbacks = new Set());
    callbacks.add(callback);
    return () => callbacks!.delete(callback);
  }
  emit(event: string, value: string | number): void { for (const cb of this.handlers.get(event) ?? []) cb(value); }
  reply(id: number, result: unknown): void { this.emit("stdout", `${JSON.stringify({ id, result })}\n`); }
  request(id: number, method: string, params: unknown): void { this.emit("stdout", `${JSON.stringify({ id, method, params })}\n`); }
  notification(method: string, params: unknown): void { this.emit("stdout", `${JSON.stringify({ method, params })}\n`); }
  requestAt(index: number): any { return JSON.parse(this.writes[index]); }
}
const testSchedule = (ms: number, callback: () => void): (() => void) => {
  const timer = setTimeout(callback, ms);
  return () => clearTimeout(timer);
};
async function waitForWrites(transport: MockTransport, count: number): Promise<void> {
  for (let i = 0; i < 20 && transport.writes.length < count; i++) await new Promise(resolve => setImmediate(resolve));
  assert.ok(transport.writes.length >= count, `expected at least ${count} writes`);
}

test("connect sequences initialize, initialized, then account/read across split NDJSON chunks", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule });
  const connected: string[] = [];
  client.on(event => { if (event.type === "connected") connected.push(event.userAgent ?? ""); });
  const connecting = client.connect();
  assert.equal(transport.requestAt(0).method, "initialize");
  transport.emit("stdout", `{"id":1,"result":{"userAgent":"codex-test"}}\n`);
  await waitForWrites(transport, 3);
  assert.equal(transport.writes[1], '{"method":"initialized","params":{}}\n');
  assert.equal(transport.requestAt(2).method, "account/read");
  transport.reply(2, { account: { type: "chatgpt" } });
  assert.deepEqual(await connecting, { account: { type: "chatgpt" } });
  assert.deepEqual(connected, ["codex-test"]);
  assert.equal(client.isConnected, true);
  client.dispose();
});

test("thread, turn, delta stream, explicit approvals, and interrupt route through JSON-RPC", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule, cwd: "/configured/cwd" });
  const events: any[] = [];
  client.on(event => events.push(event));
  const connecting = client.connect();
  transport.reply(1, { userAgent: "test" });
  await waitForWrites(transport, 3);
  transport.reply(2, { account: { authenticated: true } });
  await connecting;
  const starting = client.newThread();
  assert.deepEqual(transport.requestAt(3).params, { cwd: "/configured/cwd", approvalPolicy: "on-request", sandbox: "read-only" });
  transport.reply(3, { thread: { id: "thread-1" } });
  assert.equal(await starting, "thread-1");
  const sending = client.send("hello");
  assert.deepEqual(transport.requestAt(4).params.input, [{ type: "text", text: "hello", text_elements: [] }]);
  transport.reply(4, { turn: { id: "turn-1" } });
  assert.equal(await sending, "turn-1");
  await assert.rejects(client.send("overlap"), /already starting or running/);
  transport.emit("stdout", '{"method":"item/agentMessage/delta","params":{"threadId":"thread-1","delta":"Hi"}}\n');
  transport.request(90, "item/commandExecution/requestApproval", { command: "rm -rf /" });
  transport.request(91, "item/tool/requestUserInput", { questions: [] });
  transport.request(92, "execCommandApproval", { command: ["make"], cwd: "/tmp" });
  assert.equal(client.transcript, "Hi");
  assert.equal(events.find(e => e.type === "delta")?.text, "Hi");
  assert.equal(events.find(e => e.id === 90)?.type, "approval");
  assert.equal(events.find(e => e.id === 91)?.type, "user-input");
  assert.equal(transport.writes.length, 5, "server requests are never auto-approved");
  client.resolveApproval(90, "accept");
  client.respond(91, { answers: [] });
  client.resolveApproval(92, "decline", "No thanks");
  assert.deepEqual(JSON.parse(transport.writes[5]), { id: 90, result: { decision: "accept" } });
  assert.deepEqual(JSON.parse(transport.writes[6]), { id: 91, result: { answers: [] } });
  assert.deepEqual(JSON.parse(transport.writes[7]), { id: 92, result: { decision: { denied: { rejection: "No thanks" } } } });
  const cancelling = client.cancel();
  assert.equal(transport.requestAt(8).method, "turn/interrupt");
  assert.deepEqual(transport.requestAt(8).params, { threadId: "thread-1", turnId: "turn-1" });
  transport.reply(5, {});
  await cancelling;
  transport.notification("turn/started", { threadId: "thread-1", turn: { id: "turn-1" } });
  transport.notification("turn/completed", { threadId: "thread-1", turn: { id: "turn-1" } });
  assert.deepEqual(events.filter(e => e.method?.startsWith("turn/")).map(e => e.method), ["turn/started", "turn/completed"]);
  assert.equal(client.currentThreadId, "thread-1");
  client.dispose();
});

test("cancel waits for the in-flight turn/start response before interrupting it", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule });
  const connecting = client.connect();
  transport.reply(1, { userAgent: "test" });
  await waitForWrites(transport, 3);
  transport.reply(2, {});
  await connecting;
  const creating = client.newThread();
  transport.reply(3, { thread: { id: "thread-race" } });
  await creating;

  const sending = client.send("start then stop");
  const cancelling = client.cancel();
  assert.equal(transport.writes.length, 5, "interrupt waits for the pending turn/start response");
  transport.reply(4, { turn: { id: "turn-race" } });
  assert.equal(await sending, "turn-race");
  await waitForWrites(transport, 6);
  assert.deepEqual(transport.requestAt(5), {
    id: 5, method: "turn/interrupt", params: { threadId: "thread-race", turnId: "turn-race" }
  });
  transport.reply(5, {});
  await cancelling;
  client.dispose();
});

test("newThread clears only the in-memory transcript after its thread starts", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule });
  const connecting = client.connect();
  transport.reply(1, { userAgent: "test" });
  await waitForWrites(transport, 3);
  transport.reply(2, {});
  await connecting;
  const first = client.newThread();
  transport.reply(3, { thread: { id: "first" } });
  await first;
  transport.notification("item/agentMessage/delta", { threadId: "first", turnId: "old", itemId: "a", delta: "old transcript" });
  assert.equal(client.transcript, "old transcript");
  const second = client.newThread();
  assert.equal(client.transcript, "old transcript", "keep old state until thread/start succeeds");
  transport.reply(4, { thread: { id: "second" } });
  assert.equal(await second, "second");
  assert.equal(client.transcript, "");
  client.dispose();
});

test("item lifecycle events keep agent messages separate and normalize bounded tool/model details", () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule, maxItemFieldChars: 16 });
  const events: any[] = [];
  client.on(event => events.push(event));
  const agentStart = { threadId: "th", turnId: "tu", item: { type: "agentMessage", id: "answer-1", text: "First final response" } };
  transport.notification("item/started", { ...agentStart, item: { ...agentStart.item, text: "" } });
  transport.notification("item/agentMessage/delta", { threadId: "th", turnId: "tu", itemId: "answer-1", delta: "streamed" });
  transport.notification("item/completed", agentStart);
  transport.notification("item/completed", { threadId: "th", turnId: "tu", item: { type: "agentMessage", id: "answer-2", text: "No deltas here" } });
  transport.notification("item/started", { threadId: "th", turnId: "tu", item: { type: "commandExecution", id: "cmd-1", command: "npm run build", cwd: "/repo", status: "inProgress" } });
  transport.notification("item/commandExecution/outputDelta", { threadId: "th", turnId: "tu", itemId: "cmd-1", delta: "build output" });
  transport.notification("item/completed", { threadId: "th", turnId: "tu", item: { type: "commandExecution", id: "cmd-1", command: "npm run build", cwd: "/repo", status: "completed", exitCode: 0, aggregatedOutput: "successful build output" } });
  transport.notification("item/completed", { threadId: "th", turnId: "tu", item: { type: "fileChange", id: "file-1", status: "applied", changes: [{ path: "src/a.ts", kind: "update", diff: "+changed" }] } });
  transport.notification("item/completed", { threadId: "th", turnId: "tu", item: { type: "mcpToolCall", id: "mcp-1", server: "docs", tool: "search", status: "completed", arguments: { query: "x" }, result: { structuredContent: { title: "Found" } } } });
  transport.notification("item/completed", { threadId: "th", turnId: "tu", item: { type: "webSearch", id: "web-1", query: "weather", results: [{ title: "A" }] } });
  transport.notification("item/completed", { threadId: "th", turnId: "tu", item: { type: "imageGeneration", id: "image-1", status: "completed", revisedPrompt: "A tiny picture", result: "huge inline payload", savedPath: "/tmp/image.png" } });
  transport.notification("turn/plan/updated", { threadId: "th", turnId: "tu", explanation: "Do work", plan: [{ step: "Build", status: "inProgress" }] });
  transport.notification("turn/diff/updated", { threadId: "th", turnId: "tu", diff: "0123456789abcdef-long-diff" });
  transport.notification("some/future/activity", { ok: true });

  const items = events.filter(event => event.type === "item");
  assert.deepEqual(items.slice(0, 4).map(event => [event.itemId, event.phase]), [
    ["answer-1", "started"], ["answer-1", "output"], ["answer-1", "completed"], ["answer-2", "completed"]
  ]);
  assert.equal(items[2].output, "First final res…", "completed text is a fallback when streaming deltas are absent");
  assert.equal(items[3].itemId, "answer-2", "distinct replies keep distinct item IDs");
  const command = items.find(event => event.itemType === "commandExecution" && event.phase === "completed");
  assert.equal(command.detail, "/repo");
  assert.equal(command.success, true);
  assert.equal(command.output.length, 16);
  assert.equal(items.find(event => event.itemType === "fileChange")?.output.includes("src/a.ts"), true);
  assert.equal(items.find(event => event.itemType === "mcpToolCall")?.summary, "docs: search");
  assert.equal(items.find(event => event.itemType === "webSearch")?.summary, "weather");
  const image = items.find(event => event.itemType === "imageGeneration");
  assert.equal(image.output, "/tmp/image.png");
  assert.equal(items.find(event => event.itemType === "plan")?.phase, "progress");
  assert.equal(items.find(event => event.itemType === "diff")?.output.length, 16);
  assert.equal(events.some(event => event.type === "activity" && event.method === "some/future/activity"), true);
  client.dispose();
});

test("disconnect rejects outstanding requests and disposal rejects new work", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule });
  const connecting = client.connect();
  transport.emit("exit", 7);
  await assert.rejects(connecting, /exited \(7\)/);
  assert.equal(client.isConnected, false);
  assert.throws(() => client.respond(1, {}), /closed/);
  client.dispose();
  assert.equal(transport.killed, false, "already exited process needs no kill");
});

test("oversized NDJSON line closes the protocol and rejects pending work", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, maxLineBytes: 32, schedule: testSchedule });
  const connecting = client.connect();
  transport.emit("stdout", "x".repeat(33));
  await assert.rejects(connecting, /exceeded configured limit/);
  assert.equal(client.isConnected, false);
  client.dispose();
});

test("large JSONL messages split into many chunks are joined once and still dispatch", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule, maxLineBytes: 4096 });
  const connecting = client.connect();
  const wire = `${JSON.stringify({ id: 1, result: { userAgent: "x".repeat(1800) } })}\n`;
  for (let i = 0; i < wire.length; i++) transport.emit("stdout", wire[i]);
  await waitForWrites(transport, 3);
  transport.reply(2, { account: { ok: true } });
  assert.deepEqual(await connecting, { account: { ok: true } });
  client.dispose();
});

test("an oversized incomplete tail after a valid line still closes the connection", async () => {
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule, maxLineBytes: 32 });
  const connecting = client.connect();
  transport.emit("stdout", `${JSON.stringify({ id: 1, result: { userAgent: "ok" } })}\n${"x".repeat(33)}`);
  await assert.rejects(connecting, /exceeded configured limit/);
  assert.equal(client.isConnected, false);
  client.dispose();
});

test("listener exceptions are reported through core logging and do not break protocol delivery", () => {
  const logs: string[] = [];
  const bridge: NativeBridge = {
    create() { return 1; }, update() {}, remove() {}, log(message) { logs.push(message); },
    invoke() { return "null"; }, stats() { return {}; }
  };
  globalThis.__cp = bridge;
  const transport = new MockTransport();
  const client = new CodexClient({ transport, schedule: testSchedule });
  let reached = false;
  client.on(() => { throw new Error("render crashed"); });
  client.on(event => { if (event.type === "activity" && event.method === "future/event") reached = true; });
  transport.notification("future/event", { value: 1 });
  assert.equal(reached, true);
  assert.match(logs[0], /Codex event listener failed: render crashed/);
  client.dispose();
  globalThis.__cp = undefined;
});

test("connectCodex starts the CLI with configured cwd and app-server stdio args", () => {
  const transport = new MockTransport();
  let opts: { command: string; args?: string[]; cwd?: string } | undefined;
  const client = new CodexClient({ cwd: "/workspace/demo", schedule: testSchedule, spawn: options => { opts = options; return transport; } });
  assert.deepEqual(opts, { command: "codex", args: ["app-server", "--stdio"], cwd: "/workspace/demo" });
  client.dispose();
});

test("resume preserves configured cwd and requests use injected core-style scheduler", async () => {
  const transport = new MockTransport();
  const callbacks: Array<() => void> = [];
  let scheduledDelay = 0;
  let cancelled = false;
  const client = new CodexClient({ transport, cwd: "/worktree", requestTimeoutMs: 1234, schedule: (ms, callback) => {
    scheduledDelay = ms; callbacks.push(callback); return () => { cancelled = true; };
  } });
  const connecting = client.connect();
  assert.equal(scheduledDelay, 1234);
  transport.reply(1, { userAgent: "test" });
  await waitForWrites(transport, 3);
  assert.equal(cancelled, true);
  transport.reply(2, {});
  await connecting;
  const resuming = client.resume("thread-existing");
  assert.equal(transport.requestAt(3).method, "thread/resume");
  assert.deepEqual(transport.requestAt(3).params, { threadId: "thread-existing", cwd: "/worktree" });
  transport.reply(3, {});
  await resuming;
  assert.equal(client.currentThreadId, "thread-existing");
  client.dispose();
});
