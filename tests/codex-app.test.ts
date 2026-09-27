import test from "node:test";
import assert from "node:assert/strict";
import { after, type NativeBridge } from "../packages/core/src/index.js";
import { CodexClient, type CodexTransport } from "../packages/codex/src/index.js";
import { createCodexApp } from "../examples/codex/src/app.js";

class MockTransport implements CodexTransport {
  writes: string[] = [];
  handlers = new Map<string, Set<(value: string | number) => void>>();
  write(text: string): void { this.writes.push(text); }
  closeStdin(): void {}
  kill(): void {}
  dispose(): void {}
  on(event: "exit", callback: (value: number) => void): () => void;
  on(event: "stdout" | "stderr" | "error", callback: (value: string) => void): () => void;
  on(event: "stdout" | "stderr" | "exit" | "error", callback: (value: any) => void): () => void {
    let set = this.handlers.get(event);
    if (!set) this.handlers.set(event, set = new Set());
    set.add(callback);
    return () => set!.delete(callback);
  }
  emit(event: string, data: string | number): void { for (const fn of this.handlers.get(event) ?? []) fn(data); }
  reply(id: number, result: unknown): void { this.emit("stdout", `${JSON.stringify({ id, result })}\n`); }
  request(id: number, method: string, params: unknown): void { this.emit("stdout", `${JSON.stringify({ id, method, params })}\n`); }
  notify(method: string, params: unknown): void { this.emit("stdout", `${JSON.stringify({ method, params })}\n`); }
  requestAt(index: number): any { return JSON.parse(this.writes[index]); }
}

function makeTestApp() {
  let nextId = 0;
  const widgets = new Map<number, { kind: string; parent: number; props: Record<string, unknown> }>();
  const commands: Array<{ id: number; action: string; arg?: unknown }> = [];
  const trapped = new Set<number>();
  const implicitFocusReleases: number[] = [];
  const created: Array<{ id: number; kind: string; parent: number; props: Record<string, unknown> }> = [];
  const transport = new MockTransport();
  let client!: CodexClient;
  const bridge: NativeBridge = {
    create(kind, parent, props) {
      const id = ++nextId;
      const node = { kind, parent, props: { ...props } };
      widgets.set(id, node); created.push({ id, kind, parent, props: node.props });
      return id;
    },
    update(id, props) { const node = widgets.get(id); if (node) Object.assign(node.props, props); },
    remove(id) { widgets.delete(id); if (trapped.delete(id)) implicitFocusReleases.push(id); },
    log() {}, stats() { return { live: widgets.size }; },
    invoke(service, method) {
      if (service === "platform" && method === "get") return JSON.stringify({ cwd: "/repo", codexCommand: "codex" });
      if (service === "io" && method === "poll") return "[]";
      return "null";
    },
    command(id, action, arg) {
      commands.push({ id, action, arg });
      if (action === "trapFocus") trapped.add(id);
      else if (action === "releaseFocus") trapped.delete(id);
      if (action === "getValue") return widgets.get(id)?.props.value ?? "";
      return undefined;
    }
  };
  globalThis.__cp = bridge;
  const app = createCodexApp({ createClient: platform => {
    assert.deepEqual(platform, { cwd: "/repo", codexCommand: "codex" });
    client = new CodexClient({ transport, schedule: (ms, callback) => after(ms, callback), cwd: platform.cwd });
    return client;
  } });
  return {
    app, client: () => client, transport, widgets, commands, created, trapped, implicitFocusReleases,
    tick(now: number) { globalThis.__cpTick?.(now); },
    press(text: string) {
      const found = [...widgets].reverse().find(([, node]) => node.kind === "button" && node.props.text === text);
      assert.ok(found, `missing button: ${text}`);
      globalThis.__cpDispatch?.(found[0], "press");
      return found[0];
    },
    inputValue(value: string) {
      const found = [...widgets].find(([, node]) => node.kind === "input");
      assert.ok(found, "missing composer");
      found[1].props.value = value;
      globalThis.__cpDispatch?.(found[0], "change", value);
    },
    dispose() { app.dispose(); globalThis.__cp = undefined; }
  };
}

async function flush(): Promise<void> {
  for (let i = 0; i < 12; i++) await new Promise(resolve => setImmediate(resolve));
}

function isReadableInk(value: string): boolean {
  const match = /^#([\da-f]{6})$/i.exec(value);
  if (!match) return false;
  const channels = [0, 2, 4].map(offset => parseInt(match[1].slice(offset, offset + 2), 16) / 255);
  const linear = channels.map(channel => channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4);
  return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2] >= 0.6;
}

async function startDemo(testApp: ReturnType<typeof makeTestApp>): Promise<void> {
  testApp.tick(1);
  await flush();
  assert.equal(testApp.transport.requestAt(0).method, "initialize");
  testApp.transport.reply(1, { userAgent: "test" });
  await flush();
  assert.equal(testApp.transport.requestAt(2).method, "account/read");
  testApp.transport.reply(2, { account: { authenticated: true } });
  await flush();
  assert.equal(testApp.transport.requestAt(3).method, "thread/start");
  testApp.transport.reply(3, { thread: { id: "thread-old" } });
  await flush();
}

test("New clears widgets immediately while busy, cancels, and ignores late old-thread output", async () => {
  const t = makeTestApp();
  try {
    await startDemo(t);
    const threadReply = { threadId: "thread-old", turnId: "turn-old", item: { type: "agentMessage", id: "old-message", text: "Old response" } };
    t.transport.notify("item/completed", threadReply);
    t.transport.notify("item/started", { threadId: "thread-old", turnId: "turn-old", item: { type: "commandExecution", id: "old-tool", command: "npm run build", cwd: "/repo", status: "inProgress" } });
    t.transport.notify("item/completed", { threadId: "thread-old", turnId: "turn-old", item: { type: "commandExecution", id: "old-tool", command: "npm run build", cwd: "/repo", status: "completed", exitCode: 0 } });
    t.tick(34);
    assert.ok([...t.widgets.values()].some(node => JSON.stringify(node.props).includes("Old response")));
    assert.ok([...t.widgets.values()].some(node => node.kind === "button" && String(node.props.text).includes("Done: npm run build")));

    t.inputValue("start work");
    t.press("Send");
    await flush();
    assert.ok(t.commands.some(command => command.action === "getValue"));
    assert.ok(t.commands.some(command => command.action === "focus"));
    assert.equal(t.transport.requestAt(4).method, "turn/start");
    t.press("New");
    assert.equal([...t.widgets.values()].some(node => JSON.stringify(node.props).includes("Old response")), false,
      "the old conversation widgets are removed synchronously before cancel awaits");
    assert.equal([...t.widgets.values()].some(node => String(node.props.text).includes("Done: npm run build")), false,
      "old tool cards are removed with the conversation");

    t.transport.notify("item/agentMessage/delta", { threadId: "thread-old", turnId: "turn-old", itemId: "late", delta: "late old answer" });
    assert.equal([...t.widgets.values()].some(node => JSON.stringify(node.props).includes("late old answer")), false);
    t.transport.reply(4, { turn: { id: "turn-active" } });
    await flush();
    assert.equal(t.transport.requestAt(5).method, "turn/interrupt");
    t.transport.reply(5, {});
    await flush();
    assert.equal(t.transport.requestAt(6).method, "thread/start");
    t.transport.notify("item/agentMessage/delta", { threadId: "thread-old", turnId: "turn-old", itemId: "late-starting", delta: "old while next starts" });
    assert.equal([...t.widgets.values()].some(node => JSON.stringify(node.props).includes("old while next starts")), false);
    t.transport.reply(6, { thread: { id: "thread-new" } });
    await flush();
    t.transport.notify("item/agentMessage/delta", { threadId: "thread-old", turnId: "turn-old", itemId: "late-2", delta: "still old" });
    assert.equal([...t.widgets.values()].some(node => JSON.stringify(node.props).includes("still old")), false);
  } finally { t.dispose(); }
});

test("approval dialog shows readable details, traps/releases focus, and sends exact allow/deny decisions", async () => {
  const t = makeTestApp();
  try {
    await startDemo(t);
    t.transport.request(90, "item/commandExecution/requestApproval", {
      threadId: "thread-old", turnId: "turn-old", itemId: "cmd", command: "rm -rf /tmp/demo", cwd: "/repo", reason: "Needs write access"
    });
    const detail = [...t.widgets.values()].find(node => node.kind === "label" && String(node.props.text).includes("rm -rf /tmp/demo"));
    assert.ok(detail, "approval details are displayed");
    assert.ok(isReadableInk(String(detail.props.color)), `approval text uses a bright ink color: ${JSON.stringify(detail.props)}`);
    const allow = [...t.widgets].find(([, node]) => node.kind === "button" && node.props.text === "Allow once");
    assert.ok(allow);
    assert.ok(t.commands.some(command => command.action === "trapFocus"), "approval dialog traps focus");
    t.press("Allow once");
    assert.deepEqual(t.transport.requestAt(4), { id: 90, result: { decision: "accept" } });
    assert.equal(t.trapped.size, 0, "removing the native dialog releases its focus trap");
    assert.ok(t.implicitFocusReleases.length > 0);

    t.transport.request(91, "item/fileChange/requestApproval", { threadId: "thread-old", turnId: "turn-old", itemId: "file", changes: [{ path: "a.ts", diff: "+x" }] });
    t.press("Deny");
    assert.deepEqual(t.transport.requestAt(5), { id: 91, result: { decision: "decline" } });
  } finally { t.dispose(); }
});

test("tool disclosures create bodies lazily, can toggle repeatedly, and clean up on app disposal", async () => {
  const t = makeTestApp();
  try {
    await startDemo(t);
    t.transport.notify("item/started", { threadId: "thread-old", turnId: "turn-1", item: { type: "commandExecution", id: "cmd-1", command: "npm run build", cwd: "/repo", status: "inProgress" } });
    t.transport.notify("item/completed", { threadId: "thread-old", turnId: "turn-1", item: { type: "commandExecution", id: "cmd-1", command: "npm run build", cwd: "/repo", status: "completed", exitCode: 0, aggregatedOutput: "BUILD_RESULT" } });
    t.tick(34);
    const disclosure = [...t.widgets].find(([, node]) => node.kind === "button" && String(node.props.text).includes("Done: npm run build"));
    assert.ok(disclosure, "tool title renders with completion status");
    const countRich = () => [...t.widgets.values()].filter(node => node.kind === "richText").length;
    assert.equal(countRich(), 0, "closed disclosure has no body widgets allocated");
    globalThis.__cpDispatch?.(disclosure[0], "press");
    assert.ok(String(t.widgets.get(disclosure[0])?.props.text).startsWith("[-]"));
    assert.ok(countRich() > 0, "opening renders the markdown body on demand");
    assert.ok([...t.widgets.values()].some(node => JSON.stringify(node.props).includes("BUILD_RESULT")));
    globalThis.__cpDispatch?.(disclosure[0], "press");
    assert.ok(String(t.widgets.get(disclosure[0])?.props.text).startsWith("[+]"));
    assert.equal(countRich(), 0, "closing disposes the body subtree");
    globalThis.__cpDispatch?.(disclosure[0], "press");
    assert.ok(String(t.widgets.get(disclosure[0])?.props.text).startsWith("[-]"));
    assert.ok(countRich() > 0, "the body can be reopened after disposal");
  } finally {
    t.dispose();
    assert.equal(t.widgets.size, 0, "app disposal releases all native widgets");
  }
});

test("user-question option responds with the expected answer payload", async () => {
  const t = makeTestApp();
  try {
    await startDemo(t);
    t.transport.request(92, "item/tool/requestUserInput", {
      threadId: "thread-old", turnId: "turn-old", itemId: "question", isBlocking: true,
      questions: [{ id: "q1", header: "Preference", question: "Choose one", isOther: false, isSecret: false, options: [{ label: "Keep it simple", description: "" }] }]
    });
    assert.ok(t.commands.some(command => command.action === "trapFocus"));
    t.press("Keep it simple");
    assert.deepEqual(t.transport.requestAt(4), { id: 92, result: { answers: { q1: { answers: ["Keep it simple"] } } } });
    assert.equal(t.trapped.size, 0, "removing the question dialog releases its focus trap");
  } finally { t.dispose(); }
});
