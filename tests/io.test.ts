import test from "node:test";
import assert from "node:assert/strict";
import { createApp, http, processes, type NativeBridge } from "../packages/core/src/index.js";

function mockHost() {
  let nextWidget = 1;
  let nextHandle = 100;
  const invocations: Array<{ service: string; method: string; args: unknown[] }> = [];
  const events: Array<Record<string, unknown>> = [];
  const logs: string[] = [];
  const host: NativeBridge = {
    create() { return nextWidget++; }, update() {}, remove() {}, log(message) { logs.push(message); }, stats() { return {}; },
    command() {},
    invoke(service, method, jsonArgs) {
      const args = JSON.parse(jsonArgs) as unknown[];
      invocations.push({ service, method, args });
      if (service === "process" && method === "spawn") return JSON.stringify(nextHandle++);
      if (service === "http" && method === "start") return JSON.stringify(nextHandle++);
      if (service === "io" && method === "poll") return JSON.stringify(events.splice(0));
      return "null";
    }
  };
  globalThis.__cp = host;
  return { invocations, events, logs };
}

test("processes dispatch output before exit and stop dispatching after disposal", () => {
  const host = mockHost();
  let process!: ReturnType<typeof processes.spawn>;
  const stdout: string[] = [];
  const stderr: string[] = [];
  const exits: number[] = [];
  const app = createApp({ setup() {
    process = processes.spawn({ command: "codex", args: ["app-server", "--stdio"], cwd: "/tmp/project" });
    process.on("stdout", value => stdout.push(value));
    process.on("stderr", value => stderr.push(value));
    process.on("exit", code => exits.push(code));
  } });
  process.write("hello\n");
  process.closeStdin();
  assert.deepEqual(host.invocations.slice(0, 3).map(x => [x.service, x.method]), [
    ["process", "spawn"], ["process", "write"], ["process", "closeStdin"]
  ]);
  host.events.push(
    { type: "process/stdout", id: process.id, data: "reply" },
    { type: "process/stderr", id: process.id, data: "warning" },
    { type: "process/exit", id: process.id, code: 0 },
  );
  globalThis.__cpTick!(1);
  assert.deepEqual(stdout, ["reply"]);
  assert.deepEqual(stderr, ["warning"]);
  assert.deepEqual(exits, [0]);
  assert.equal(process.isClosed, true);
  assert.throws(() => process.on("stdout", () => {}), /closed/);
  host.events.push({ type: "process/stdout", id: process.id, data: "stale" });
  globalThis.__cpTick!(2);
  assert.deepEqual(stdout, ["reply"]);
  app.dispose();
});

test("HTTP completion resolves through app ticks and cancellation ignores late completion", async () => {
  const host = mockHost();
  let app!: ReturnType<typeof createApp>;
  app = createApp({ setup() {} });
  const completed = http.start({ url: "https://example.test/data", timeoutMs: 500, maxBytes: 4096 });
  host.events.push({ type: "http/done", id: 100, status: 200, body: '{"ok":true}', headers: { "content-type": "application/json" } });
  globalThis.__cpTick!(10);
  const response = await completed.promise;
  assert.equal(response.ok, true);
  assert.deepEqual(response.json(), { ok: true });
  assert.equal(response.headers["content-type"], "application/json");

  const cancelled = http.start({ url: "http://example.test/slow" });
  const rejected = assert.rejects(cancelled.promise, /cancelled/);
  cancelled.cancel();
  await rejected;
  host.events.push({ type: "http/done", id: 101, status: 200, body: "late" });
  globalThis.__cpTick!(20);
  assert.equal(host.invocations.filter(x => x.service === "http" && x.method === "cancel").length, 1);
  app.dispose();
});

test("app disposal cancels outstanding native work and prevents stale callbacks", async () => {
  const host = mockHost();
  let app!: ReturnType<typeof createApp>;
  let child!: ReturnType<typeof processes.spawn>;
  let outputCalls = 0;
  app = createApp({ setup() {
    child = processes.spawn({ command: "long-running" });
    child.on("stdout", () => outputCalls++);
  } });
  const task = http.start({ url: "https://example.test/pending" });
  const rejected = assert.rejects(task.promise, /disposed/);
  app.dispose();
  await rejected;
  assert.equal(child.isClosed, true);
  assert.ok(host.invocations.some(x => x.service === "process" && x.method === "kill"));
  assert.ok(host.invocations.some(x => x.service === "http" && x.method === "cancel"));
  host.events.push({ type: "process/stdout", id: child.id, data: "stale" }, { type: "http/done", id: 101, status: 200 });
  assert.equal(globalThis.__cpTick, undefined);
  assert.equal(outputCalls, 0);
});

test("a throwing process listener does not drop later listeners or events from the drained batch", async () => {
  const host = mockHost();
  const seen: string[] = [];
  let child!: ReturnType<typeof processes.spawn>;
  const app = createApp({ setup() {
    child = processes.spawn({ command: "codex" });
    child.on("stdout", () => { throw new Error("render failed"); });
    child.on("stdout", value => seen.push(`stdout:${value}`));
    child.on("exit", code => seen.push(`exit:${code}`));
  } });
  const task = http.start({ url: "https://example.test/result" });
  const done = task.promise;
  host.events.push(
    { type: "process/stdout", id: child.id, data: "chunk" },
    { type: "process/exit", id: child.id, code: 7 },
    { type: "http/done", id: 101, status: 201, body: "created" },
  );
  globalThis.__cpTick!(1);
  const response = await done;
  assert.equal(response.status, 201);
  assert.equal(response.body, "created");
  assert.deepEqual(seen, ["stdout:chunk", "exit:7"]);
  assert.ok(host.logs.some(message => message.includes("render failed")));
  app.dispose();
});

test('native queue failure closes every child and rejects every HTTP waiter', async () => {
  const host = mockHost();
  const app = createApp({ setup() {} });
  const child = processes.spawn({ command: 'noisy' });
  const seen: Array<string | number> = [];
  child.on('error', value => seen.push(value));
  child.on('exit', value => seen.push(value));
  const task = http.start({ url: 'https://example.test/slow' });
  const rejected = assert.rejects(task.promise, /overflow/);
  host.events.push({ type: 'io/error', error: 'event queue overflow' });
  globalThis.__cpTick!(1);
  await rejected;
  assert.deepEqual(seen, ['event queue overflow', -1]);
  assert.equal(child.isClosed, true);
  app.dispose();
});
