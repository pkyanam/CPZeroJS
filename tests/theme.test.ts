import test from "node:test";
import assert from "node:assert/strict";
import { createApp, ui, type NativeBridge } from "../packages/core/src/index.js";
import { createTheme, defineRecipe, defaultTheme, scopedTheme } from "../packages/core/src/theme.js";

function mockHost() {
  let id = 0;
  const created: Array<{ id: number; kind: string; props: Record<string, unknown> }> = [];
  const updates: Array<{ id: number; props: Record<string, unknown> }> = [];
  const host: NativeBridge = {
    create(kind, _parent, props) { const next = ++id; created.push({ id: next, kind, props }); return next; },
    update(widgetId, props) { updates.push({ id: widgetId, props }); }, remove() {}, log() {}, stats() { return {}; },
    invoke() { return "null"; }
  };
  globalThis.__cp = host;
  return { created, updates };
}

const actionButton = defineRecipe({
  base: theme => ({ radius: theme.radius.md, paddingX: theme.spacing.sm, paddingY: theme.spacing.xs, fontSize: theme.fontSize.md }),
  variants: {
    tone: {
      primary: theme => ({ bg: theme.colors.primary, color: theme.colors.primaryForeground }),
      quiet: theme => ({ bg: theme.colors.muted, color: theme.colors.mutedForeground })
    },
    size: {
      compact: theme => ({ height: 18, paddingX: theme.spacing.xs }),
      regular: theme => ({ height: 22 })
    }
  },
  defaultVariants: { tone: "primary", size: "regular" }
});

test("createTheme merges partial semantic tokens and returns immutable values", () => {
  const theme = createTheme({ colors: { primary: "#224466" }, radius: { md: 5 } }, { spacing: { xs: 1 } });
  assert.equal(theme.colors.primary, "#224466");
  assert.equal(theme.colors.foreground, defaultTheme.colors.foreground);
  assert.equal(theme.radius.md, 5);
  assert.equal(theme.spacing.xs, 1);
  assert.ok(Object.isFrozen(theme));
  assert.ok(Object.isFrozen(theme.colors));
  assert.throws(() => createTheme({ colors: { primary: "red" } }), /#RRGGBB/);
});

test("recipes merge base, defaults, and explicit variants into native widget props", () => {
  const theme = createTheme({ colors: { primary: "#112233" }, spacing: { xs: 1 } });
  assert.deepEqual(actionButton.resolve(theme, { tone: "quiet", size: "compact" }), {
    radius: 4, paddingX: 1, paddingY: 1, fontSize: 12,
    bg: theme.colors.muted, color: theme.colors.mutedForeground, height: 18
  });
  assert.equal(actionButton.resolve(theme).bg, "#112233");
  assert.throws(() => actionButton.resolve(theme, { tone: "missing" as never }), /Unknown recipe variant/);
});

test("scoped themes update bound widgets and release observers with widget disposal", () => {
  const host = mockHost();
  const theme = scopedTheme();
  let button!: ReturnType<typeof ui.button>;
  const app = createApp({ setup(root) {
    button = ui.button(root, { text: "OK" });
    theme.apply(button, actionButton);
  } });
  assert.equal(host.updates.at(-1)?.props.bg, defaultTheme.colors.primary);
  theme.set({ colors: { primary: "#445566" }, radius: { md: 7 } });
  assert.deepEqual(host.updates.at(-1)?.props, {
    radius: 7, paddingX: 4, paddingY: 2, fontSize: 12, bg: "#445566", color: "#071712", height: 22
  });
  const beforeDispose = host.updates.length;
  button.dispose();
  theme.set({ colors: { primary: "#778899" } });
  assert.equal(host.updates.length, beforeDispose);
  app.dispose();
});

test("theme scopes do not observe changes in other scopes", () => {
  const one = scopedTheme();
  const two = scopedTheme();
  let observed = 0;
  const unsubscribe = one.subscribe(() => observed++);
  two.set({ colors: { primary: "#123456" } });
  assert.equal(observed, 0);
  one.set({ colors: { primary: "#654321" } });
  assert.equal(observed, 1);
  unsubscribe();
});
