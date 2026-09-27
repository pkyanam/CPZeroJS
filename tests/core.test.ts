import test from "node:test";
import assert from "node:assert/strict";
import { after, bind, createApp, every, services, signal, storage, ui, type NativeBridge, type Widget } from "../packages/core/src/index.js";

function mockHost() {
  let nextId = 1;
  const created: Array<{ id: number; kind: string; parent: number; props: Record<string, unknown> }> = [];
  const updates: Array<{ id: number; props: Record<string, unknown> }> = [];
  const removed: number[] = [];
  const invocations: Array<{ service: string; method: string; args: unknown[] }> = [];
  const host: NativeBridge = {
    create(kind, parent, props) { const id = nextId++; created.push({ id, kind, parent, props }); return id; },
    update(id, props) { updates.push({ id, props }); },
    remove(id) { removed.push(id); }, log() {}, stats() { return { live: created.length - removed.length }; },
    invoke(service, method, jsonArgs) {
      const args = JSON.parse(jsonArgs) as unknown[];
      invocations.push({ service, method, args });
      return method === "get" ? JSON.stringify(args[0] === "missing" ? null : "saved") : "null";
    }
  };
  globalThis.__cp = host;
  return { created, updates, removed, invocations };
}

test("composes native widgets, forwards props and events, and disposes the subtree", () => {
  const host = mockHost();
  let presses = 0;
  let changed = "";
  const app = createApp({ title: "Demo", setup(root) {
    const column = ui.column(root, { gap: 4 });
    ui.button(column, { text: "Go", onPress: () => presses++ });
    ui.input(column, { value: "start", onChange: value => changed = value });
  } });
  assert.deepEqual(host.created.map(x => x.kind), ["screen", "column", "button", "input"]);
  assert.equal(host.created[2].parent, host.created[1].id);
  const dispatch = globalThis.__cpDispatch!;
  dispatch(host.created[2].id, "press");
  dispatch(host.created[3].id, "change", "typed");
  assert.equal(presses, 1);
  assert.equal(changed, "typed");
  app.dispose();
  app.dispose();
  assert.equal(host.removed.length, 4);
  dispatch(host.created[2].id, "press");
  assert.equal(presses, 1, "disposed handlers must not run");
});

test("signal binding updates only on changed values and detaches with its widget", () => {
  const host = mockHost();
  let label!: ReturnType<typeof ui.label>;
  const value = signal("first");
  const app = createApp({ setup(root) { label = ui.label(root); bind(label, "text", value); } });
  assert.equal(host.updates.at(-1)?.props.text, "first");
  const before = host.updates.length;
  value.value = "first";
  assert.equal(host.updates.length, before);
  value.value = "second";
  assert.equal(host.updates.at(-1)?.props.text, "second");
  label.dispose();
  const afterDispose = host.updates.length;
  value.value = "third";
  assert.equal(host.updates.length, afterDispose);
  app.dispose();
});

test("updating a declarative event handler replaces the previous callback", () => {
  const host = mockHost();
  let oldCalls = 0;
  let newCalls = 0;
  let button!: ReturnType<typeof ui.button>;
  const app = createApp({ setup(root) {
    button = ui.button(root, { onPress: () => oldCalls++ });
    button.update({ onPress: () => newCalls++ });
  } });
  globalThis.__cpDispatch!(button.id, "press");
  assert.equal(oldCalls, 0);
  assert.equal(newCalls, 1);
  app.dispose();
});

test("a throwing widget cleanup does not interrupt later cleanup or native removal", () => {
  const host = mockHost();
  let released = 0;
  let button!: Widget;
  const app = createApp({ setup(root) {
    button = ui.button(root, { onPress: () => assert.fail("stale prop handler called") });
    button.own(() => { throw new Error("cleanup failure"); });
    button.own(() => { released++; });
  } });
  button.dispose();
  assert.equal(released, 1);
  assert.ok(host.removed.includes(button.id));
  app.dispose();
});

test("pagedList bounds native rows by page size, navigates, replaces items, and handles empty", () => {
  const host = mockHost();
  let list!: ReturnType<typeof ui.pagedList<number>>;
  let parent!: Widget;
  const app = createApp({ setup(root) {
    parent = ui.column(root);
    list = ui.pagedList(parent, {
      items: Array.from({ length: 10_000 }, (_, i) => i),
      rowHeight: 20,
      height: 60,
      renderRow(item, index, row) { ui.label(row, { text: `${index}: ${item}` }); }
    });
  } });
  assert.equal(list.pageSize, 3);
  assert.equal(list.pageCount, 3334);
  const activeIds = () => new Set(host.created.map(item => item.id).filter(id => !host.removed.includes(id)));
  const rowCount = () => [...activeIds()].filter(id => host.created.find(item => item.id === id)?.kind === "label").length;
  assert.equal(rowCount(), 4, "three visible item labels plus the page label");
  const firstPageLive = activeIds().size;
  list.goToPage(2000);
  assert.equal(list.page, 2000);
  assert.equal(rowCount(), 4);
  assert.equal(activeIds().size, firstPageLive, "paging should keep the live native widget count bounded");
  list.setItems([42, 43]);
  assert.equal(list.page, 0);
  assert.equal(list.pageCount, 1);
  list.setItems([]);
  assert.equal(list.pageCount, 0);
  assert.equal(rowCount(), 1, "empty state retains only page label, no item rows");
  list.dispose();
  assert.equal(list.isDisposed, true);
  assert.ok(host.removed.length > 0);
  app.dispose();
});

test("host ticks drive repeat and one-shot timers with cancellable cleanup", () => {
  mockHost();
  let repeated = 0;
  let once = 0;
  let cancel!: () => void;
  const app = createApp({ setup() {
    cancel = every(10, () => repeated++);
    after(15, () => once++);
  } });
  globalThis.__cpTick!(9);
  globalThis.__cpTick!(10);
  globalThis.__cpTick!(15);
  assert.equal(repeated, 1);
  assert.equal(once, 1);
  cancel();
  globalThis.__cpTick!(20);
  assert.equal(repeated, 1);
  app.dispose();
  assert.equal(globalThis.__cpTick, undefined);
});

test("service adapters are async, removable, and native storage fallback serializes arguments", async () => {
  const host = mockHost();
  const service = { base: 4, async add(this: { base: number }, a: number, b: number) { return this.base + a + b; } };
  const unregister = services.register("math", service);
  assert.equal(await services.call<number>("math", "add", 2, 3), 9);
  await assert.rejects(services.call("math", "missing"), /no method/);
  await assert.rejects(services.call("math", "toString"), /no method/);
  unregister();
  assert.equal(await storage.get("theme"), "saved");
  assert.equal(await storage.get("missing"), null);
  assert.equal(await storage.set("theme", { mode: "dark" }), undefined);
  assert.deepEqual(host.invocations.at(-1), { service: "storage", method: "set", args: ["theme", { mode: "dark" }] });
  assert.ok(host.invocations.length === 3);
});
