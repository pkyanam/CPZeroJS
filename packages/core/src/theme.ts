import type { Widget, WidgetProps } from "./index";

/** Semantic colors and compact dimensions supported by CPZero's native widgets. */
export type ThemeTokens = Readonly<{
  colors: Readonly<{
    background: string;
    foreground: string;
    surface: string;
    surfaceForeground: string;
    primary: string;
    primaryForeground: string;
    muted: string;
    mutedForeground: string;
    accent: string;
    accentForeground: string;
    border: string;
    focus: string;
    danger: string;
    dangerForeground: string;
  }>;
  spacing: Readonly<{ xs: number; sm: number; md: number; lg: number }>;
  radius: Readonly<{ sm: number; md: number; lg: number }>;
  fontSize: Readonly<{ sm: number; md: number; lg: number }>;
}>;

export type ThemeOverrides = {
  [K in keyof ThemeTokens]?: Partial<ThemeTokens[K]>;
};

const defaultColors = {
  background: "#101820", foreground: "#E8EEF4", surface: "#1B2732", surfaceForeground: "#E8EEF4",
  primary: "#39C5A0", primaryForeground: "#071712", muted: "#263746", mutedForeground: "#A9BAC8",
  accent: "#F3B84B", accentForeground: "#1C1608", border: "#3A4B59", focus: "#F3B84B",
  danger: "#E06464", dangerForeground: "#240909"
};

/** Merge any number of partial themes over the native-friendly default tokens. */
export function createTheme(...overrides: ThemeOverrides[]): ThemeTokens {
  const merged: any = {
    colors: { ...defaultColors },
    spacing: { xs: 2, sm: 4, md: 6, lg: 8 },
    radius: { sm: 2, md: 4, lg: 6 },
    fontSize: { sm: 10, md: 12, lg: 14 }
  };
  for (const patch of overrides) {
    if (!patch || typeof patch !== "object") throw new TypeError("Theme overrides must be an object.");
    for (const group of Object.keys(merged) as Array<keyof ThemeTokens>) {
      const values = patch[group];
      if (values !== undefined) Object.assign(merged[group], values);
    }
  }
  for (const [name, value] of Object.entries(merged.colors) as Array<[string, unknown]>) {
    if (typeof value !== "string" || !/^#[\da-f]{6}$/i.test(value)) throw new TypeError(`Theme color '${name}' must be a #RRGGBB hex value.`);
  }
  for (const group of ["spacing", "radius", "fontSize"] as const) {
    for (const [name, value] of Object.entries(merged[group]) as Array<[string, unknown]>) {
      if (typeof value !== "number" || !Number.isFinite(value) || value < 0 || (group === "fontSize" && value === 0)) {
        throw new RangeError(`Theme ${group}.${name} must be a finite ${group === "fontSize" ? "positive" : "non-negative"} number.`);
      }
    }
  }
  for (const group of Object.values(merged)) Object.freeze(group);
  return Object.freeze(merged) as ThemeTokens;
}

export const defaultTheme: ThemeTokens = createTheme();

type VariantStyles = Record<string, Record<string, WidgetProps | ((theme: ThemeTokens) => WidgetProps)>>;
export type RecipeSelections<V extends VariantStyles> = Partial<{ [K in keyof V]: keyof V[K] | null }>;
export type RecipeDefinition<V extends VariantStyles = VariantStyles> = {
  base?: WidgetProps | ((theme: ThemeTokens) => WidgetProps);
  variants?: V;
  defaultVariants?: Partial<{ [K in keyof V]: keyof V[K] }>;
};

export interface ThemeRecipe<V extends VariantStyles = VariantStyles> {
  resolve(theme: ThemeTokens, selections?: RecipeSelections<V>): WidgetProps;
}

/** Define a small reusable style recipe from native widget props and theme tokens. */
export function defineRecipe<const V extends VariantStyles>(definition: RecipeDefinition<V>): ThemeRecipe<V> {
  return {
    resolve(theme, selections = {}) {
      const output: WidgetProps = {};
      mergeStyle(output, definition.base, theme);
      const selected = { ...definition.defaultVariants, ...selections } as Record<string, string | null | undefined>;
      for (const [group, value] of Object.entries(selected)) {
        if (value == null) continue;
        const style = definition.variants?.[group]?.[value];
        if (!style) throw new RangeError(`Unknown recipe variant '${group}:${value}'.`);
        mergeStyle(output, style, theme);
      }
      return output;
    }
  };
}

function mergeStyle(output: WidgetProps, style: WidgetProps | ((theme: ThemeTokens) => WidgetProps) | undefined, theme: ThemeTokens): void {
  if (!style) return;
  Object.assign(output, typeof style === "function" ? style(theme) : style);
}

export interface ThemeScope {
  readonly value: ThemeTokens;
  set(overrides: ThemeOverrides): void;
  replace(theme: ThemeOverrides): void;
  subscribe(listener: (theme: ThemeTokens) => void): () => void;
  apply<V extends VariantStyles>(widget: Widget, recipe: ThemeRecipe<V>, selections?: RecipeSelections<V>): () => void;
}

/** A local theme scope. Bound recipes update on changes and detach with their widget. */
export function scopedTheme(initial: ThemeOverrides = {}): ThemeScope {
  let current = createTheme(initial);
  const listeners = new Set<(theme: ThemeTokens) => void>();
  const publish = (next: ThemeTokens): void => {
    current = next;
    for (const listener of [...listeners]) listener(current);
  };
  return {
    get value() { return current; },
    set(overrides) { publish(createTheme(current, overrides)); },
    replace(theme) { publish(createTheme(theme)); },
    subscribe(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    apply(widget, recipe, selections) {
      const update = (theme: ThemeTokens): void => { if (!widget.isDisposed) widget.update(recipe.resolve(theme, selections)); };
      update(current);
      listeners.add(update);
      const release = widget.own(() => listeners.delete(update));
      return () => { listeners.delete(update); release(); };
    }
  };
}
