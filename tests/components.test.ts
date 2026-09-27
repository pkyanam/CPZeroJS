import test from "node:test";
import assert from "node:assert/strict";
import { createApp, ui, type NativeBridge, type Widget } from "../packages/core/src/index.js";
import { disclosure, focusScope } from "../packages/core/src/components.js";

function mockHost() {
  let nextId = 1;
  const created: Array<{ id: number; kind: string; parent: number; props: Record<string, unknown> }> = [];
  const updated: Array<{ id: number; props: Record<string, unknown> }> = [];
  const removed: number[] = [];
  const commands: Array<{ id: number; action: string; arg?: unknown }> = [];
  globalThis.__cp = {
    create(kind, parent, props) { const id = nextId++; created.push({ id, kind, parent, props }); return id; },
    update(id, props) { updated.push({ id, props }); },
    remove(id) { removed.push(id); }, log() {}, stats() { return {}; }, invoke() { return "null"; },
    command(id, action, arg) { commands.push({ id, action, arg }); },
  } satisfies NativeBridge;
  return { created, updated, removed, commands };
}

test("disclosure creates its body only while expanded and disposes it on collapse", () => {
  const host = mockHost();
  let panel!: ReturnType<typeof disclosure>;
  let renderCount = 0;
  const toggles: boolean[] = [];
  const app = createApp({ setup(root) {
    const section = ui.column(root);
    panel = disclosure(section, {
      title: "Tools",
      header: { bg: "#182334" },
      body: { gap: 5, padding: 6 },
      render(body) { renderCount++; ui.button(body, { text: "Run" }); },
      onToggle: value => toggles.push(value),
    });
  } });
  assert.equal(panel.expanded, false);
  assert.equal(renderCount, 0, "collapsed content stays lazy");
  assert.equal(host.created.filter(item => item.kind === "button").length, 1);
  const titleUpdate = host.updated.find(item => item.id === panel.header.id);
  assert.equal(titleUpdate?.props.text, "[+] Tools");

  globalThis.__cpDispatch!(panel.header.id, "press");
  assert.equal(panel.expanded, true);
  assert.equal(renderCount, 1);
  const bodyWidget = host.created.find(item => item.parent === panel.widget.id && item.kind === "column");
  assert.ok(bodyWidget);
  const child = host.created.find(item => item.parent === bodyWidget?.id);
  assert.ok(child);
  assert.equal(host.removed.includes(bodyWidget!.id), false);

  panel.setTitle("Utilities");
  assert.equal(host.updated.at(-1)?.props.text, "[-] Utilities");
  panel.setExpanded(false);
  assert.equal(panel.expanded, false);
  assert.equal(host.removed.includes(bodyWidget!.id), true);
  assert.equal(host.removed.includes(child!.id), true);
  assert.deepEqual(toggles, [true, false]);
  app.dispose();
});

test("disclosure re-renders on re-open, supports initial expansion, and cleans up with parent", () => {
  const host = mockHost();
  let panel!: ReturnType<typeof disclosure>;
  let renders = 0;
  const app = createApp({ setup(root) {
    panel = disclosure(root, { title: "Open by default", expanded: true, render() { renders++; } });
  } });
  assert.equal(panel.expanded, true);
  assert.equal(renders, 1);
  panel.setExpanded(false);
  panel.setExpanded(true);
  assert.equal(renders, 2);
  app.dispose();
  const oldCount = host.created.length;
  panel.setExpanded(false);
  panel.setTitle("after dispose");
  assert.equal(host.created.length, oldCount);
  assert.equal(panel.expanded, true, "owner teardown releases content without mutating the last visible state");
});

test("focusScope traps on a container, honors initial focus, releases once, and auto-cleans on disposal", () => {
  const host = mockHost();
  let container!: Widget;
  let target!: Widget;
  let scope!: ReturnType<typeof focusScope>;
  const app = createApp({ setup(root) {
    container = ui.column(root);
    target = ui.button(container, { text: "Confirm" });
    scope = focusScope(container, { initial: target });
  } });
  assert.deepEqual(host.commands, [{ id: container.id, action: "trapFocus", arg: target.id }]);
  scope.dispose();
  scope.release();
  assert.deepEqual(host.commands, [
    { id: container.id, action: "trapFocus", arg: target.id },
    { id: container.id, action: "releaseFocus", arg: undefined },
  ]);
  app.dispose();
  assert.equal(host.commands.length, 2, "manual disposal unregisters lifecycle cleanup");
});

test("focusScope rejects a disposed initial target and lets native container teardown release the trap", () => {
  const host = mockHost();
  let container!: Widget;
  let target!: Widget;
  const app = createApp({ setup(root) { container = ui.column(root); target = ui.button(container); } });
  target.dispose();
  assert.throws(() => focusScope(container, { initial: target }), /disposed/);
  const scope = focusScope(container);
  assert.deepEqual(host.commands[0], { id: container.id, action: "trapFocus", arg: undefined });
  container.dispose();
  scope.dispose();
  assert.equal(host.commands.filter(command => command.action === "releaseFocus").length, 0, "native container removal releases its own focus trap");
  app.dispose();
});

test("only the top nested focus scope receives Escape", () => {
  mockHost();
  let outer!: ReturnType<typeof focusScope>;
  let inner!: ReturnType<typeof focusScope>;
  const escaped: string[] = [];
  const app = createApp({ setup(root) {
    const first = ui.column(root);
    const second = ui.column(first);
    outer = focusScope(first, { onEscape: () => escaped.push("outer") });
    inner = focusScope(second, { onEscape: () => escaped.push("inner") });
  } });
  const key = globalThis.__cpKey!;
  const modifiers = { ctrl: false, shift: false, alt: false, meta: false };
  assert.throws(() => outer.dispose(), /nesting order/);
  assert.equal(key("Escape", modifiers), true);
  assert.deepEqual(escaped, ["inner"]);
  inner.dispose();
  assert.equal(key("Escape", modifiers), true);
  assert.deepEqual(escaped, ["inner", "outer"]);
  outer.dispose();
  assert.equal(key("Escape", modifiers), false);
  app.dispose();
});
