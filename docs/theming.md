# Themes and reusable component styles

CPZero apps can share a compact set of semantic colors and dimensions, then describe button, panel, or status styles once as recipes. The theme module has no UI framework dependency: recipes resolve directly to existing native `WidgetProps`, and a theme scope only keeps subscriptions for widgets that opt into runtime changes.

## Define a theme

Colors use `#RRGGBB`, which the native host accepts. Partial overrides merge over `defaultTheme`; nested token groups merge independently.

```ts
import { createTheme } from "@cpzero/core";

const theme = createTheme({
  colors: { primary: "#55D6BE", primaryForeground: "#061713" },
  spacing: { sm: 3 },
  radius: { md: 3 }
});
```

The available semantic color names are `background`, `foreground`, `surface`, `surfaceForeground`, `primary`, `primaryForeground`, `muted`, `mutedForeground`, `accent`, `accentForeground`, `border`, `focus`, `danger`, and `dangerForeground`. Spacing has `xs`, `sm`, `md`, `lg`; radius has `sm`, `md`, `lg`; font sizes have `sm`, `md`, `lg`. Token objects are frozen after creation.

## Reuse native styles with recipes

Recipes compose ordinary widget props. The last selected variant overrides earlier values, so a variant can override the base or another variant.

```ts
import { defineRecipe, defaultTheme } from "@cpzero/core";
import { ui } from "@cpzero/core";

const buttonStyle = defineRecipe({
  base: t => ({ radius: t.radius.md, paddingX: t.spacing.sm, paddingY: t.spacing.xs }),
  variants: {
    tone: {
      primary: t => ({ bg: t.colors.primary, color: t.colors.primaryForeground }),
      quiet: t => ({ bg: t.colors.muted, color: t.colors.mutedForeground })
    }
  },
  defaultVariants: { tone: "primary" }
});

ui.button(root, { text: "Send", ...buttonStyle.resolve(defaultTheme) });
ui.button(root, { text: "Later", ...buttonStyle.resolve(defaultTheme, { tone: "quiet" }) });
```

Focus outlines are drawn inside rounded buttons and inputs so tight layouts do not clip them. Set `focusColor` to match your theme; the indicator uses no extra layout space.

For shared component helpers, accept a parent widget and return normal SDK widgets. Keep behavior such as `onPress` in the component helper; recipes are only for presentation props.

## Scoped runtime theme changes

Use `scopedTheme` only when an app needs to change its look while running. `set` merges token overrides into the current theme; `replace` starts from defaults and applies the supplied theme. Bind a widget with `scope.apply(widget, recipe, variants)`. The scope has no global observer, and the widget owns the subscription cleanup.

```ts
import { scopedTheme, defineRecipe, defaultTheme } from "@cpzero/core";

const scope = scopedTheme();
const statusStyle = defineRecipe({
  base: t => ({ bg: t.colors.surface, color: t.colors.surfaceForeground, padding: t.spacing.sm }),
  variants: { state: { warning: t => ({ bg: t.colors.accent, color: t.colors.accentForeground }) } }
});

const status = ui.label(root, { text: "Ready" });
scope.apply(status, statusStyle);
// Later, only widgets bound to this scope update.
scope.set({ colors: { surface: "#202020", surfaceForeground: "#F5F5F5" } });
```

The `defaultTheme` import above is useful for direct `recipe.resolve` calls; a scope starts with those same tokens. Static styles do not allocate theme listeners.

## Relationship to shadcn/ui

shadcn/ui describes itself as an open-code component distribution approach: component source is added to the app and adapted there, rather than consumed only as a closed component package. Its theming guide recommends semantic tokens such as `background`, `foreground`, and `primary`, with components reading those tokens. CPZero uses the same useful ideas—owned composable components and semantic tokens—through native `WidgetProps` recipes.

The shadcn web components themselves are not a runtime fit for Cardputer Zero. The official manual install uses Tailwind CSS and packages such as `class-variance-authority`, `cn`, `lucide-react`, and animation helpers; its components target React and browser DOM primitives. CPZero apps run in QuickJS with native LVGL widgets and do not provide React, DOM, CSS, or Tailwind. Copying their patterns and adapting each component to `ui.*` keeps the runtime small and makes keyboard and native behavior explicit.

References: [shadcn/ui introduction and open-code approach](https://ui.shadcn.com/docs), [theming and semantic tokens](https://ui.shadcn.com/docs/theming), and [manual installation requirements](https://ui.shadcn.com/docs/installation/manual).
