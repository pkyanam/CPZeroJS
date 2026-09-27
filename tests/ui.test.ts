import test from "node:test";
import assert from "node:assert/strict";
import { createApp, keyboard, ui, type NativeBridge } from "../packages/core/src/index.js";

function mockHost() {
  let nextId = 1;
  const commands: Array<{ id: number; action: string; arg?: unknown }> = [];
  const created: Array<{ id: number; kind: string; props: Record<string, unknown> }> = [];
  const host: NativeBridge = {
    create(kind, _parent, props) { const id = nextId++; created.push({ id, kind, props }); return id; },
    update() {}, remove() {}, log() {}, stats() { return {}; }, invoke() { return "null"; },
    command(id, action, arg) {
      commands.push({ id, action, arg });
      if (action === "getValue") return "draft";
      if (action === "getScrollY") return 14;
      return undefined;
    }
  };
  globalThis.__cp = host;
  return { commands, created };
}

test("compact widget props, callback props and registered handlers are wired and removable", () => {
  const host = mockHost();
  const received: Array<[string, string]> = [];
  let button!: ReturnType<typeof ui.button>;
  let field!: ReturnType<typeof ui.input>;
  let removeChange!: () => void;
  const app = createApp({ setup(root) {
    const panel = ui.panel(root, { width: "100%", paddingX: 3, paddingY: 4, scroll: "vertical" });
    button = ui.button(panel, { text: "Send", focusable: true, onPress: () => received.push(["press", ""]) });
    field = ui.input(panel, { multiline: true, placeholder: "Type", onSubmit: value => received.push(["submit", value]) });
    removeChange = field.on("change", value => received.push(["change", value]));
  } });
  assert.equal(host.created[1].props.paddingX, 3);
  assert.equal(host.created[2].props.focusable, true);
  globalThis.__cpDispatch!(button.id, "press");
  globalThis.__cpDispatch!(field.id, "submit", "hello");
  globalThis.__cpDispatch!(field.id, "change", "draft");
  assert.deepEqual(received, [["press", ""], ["submit", "hello"], ["change", "draft"]]);
  removeChange();
  globalThis.__cpDispatch!(field.id, "change", "ignored");
  assert.equal(received.length, 3, "unsubscribed widget handlers do not run");
  app.dispose();
  globalThis.__cpDispatch?.(button.id, "press");
  assert.equal(received.length, 3, "disposed widgets do not retain callbacks");
});

test("widget focus/scroll/value commands reach native bridge with expected arguments", () => {
  const host = mockHost();
  let field!: ReturnType<typeof ui.input>;
  const app = createApp({ setup(root) { field = ui.input(root, { focusable: true }); } });
  field.focus().scrollTo(20).scrollBy(-6).scrollToEnd();
  assert.equal(field.getValue(), "draft");
  assert.equal(field.getScrollY(), 14);
  assert.deepEqual(host.commands.map(({ action, arg }) => [action, arg]), [
    ["focus", undefined], ["scrollTo", 20], ["scrollBy", -6], ["scrollToEnd", undefined],
    ["getValue", undefined], ["getScrollY", undefined],
  ]);
  app.dispose();
});

test("keyboard handlers receive modifiers, consume keys in registration order, and clear on unsubscribe/dispose", () => {
  mockHost();
  const seen: string[] = [];
  let app!: ReturnType<typeof createApp>;
  let removeFirst!: () => void;
  app = createApp({ setup() {
    removeFirst = keyboard.on(event => { seen.push(`${event.key}:${event.meta}`); return false; });
    keyboard.on(event => { seen.push(`consume:${event.key}`); return event.key === "Enter"; });
  } });
  assert.equal(globalThis.__cpKey!("Enter", { ctrl: false, shift: false, alt: false, meta: true }), true);
  assert.deepEqual(seen, ["Enter:true", "consume:Enter"]);
  seen.length = 0;
  removeFirst();
  assert.equal(globalThis.__cpKey!("x", { ctrl: true, shift: false, alt: false, meta: false }), false);
  assert.deepEqual(seen, ["consume:x"]);
  app.dispose();
  assert.equal(globalThis.__cpKey, undefined);
});

test('zoom clamps to configured levels and only notifies on a changed step', async () => {
  const { createZoom } = await import('../packages/core/src/index.js');
  const zoom = createZoom({ levels: [10, 12, 14], initial: 12 });
  const seen: number[] = [];
  const stop = zoom.subscribe(value => seen.push(value));
  zoom.zoomIn(); zoom.zoomIn(); zoom.zoomOut(); zoom.set(9); zoom.reset();
  assert.deepEqual(seen, [14, 12, 10, 12]);
  stop(); zoom.zoomIn(); assert.equal(seen.length, 4);
  assert.throws(() => createZoom({ levels: [12, 10] }), /increasing/);
});
