/** CPZeroJS: a small, host-neutral UI and service SDK for CPZero devices. */

export type WidgetKind = "screen" | "column" | "row" | "label" | "button" | "input" | "bar" | "box";
export type WidgetValue = string | number | boolean | undefined;
export type WidgetProps = {
  text?: string; width?: number | "100%"; height?: number | "100%"; grow?: number;
  gap?: number; padding?: number; bg?: string; color?: string; fontSize?: number;
  value?: string | number; min?: number; max?: number; hidden?: boolean; disabled?: boolean;
  onPress?: () => void; onChange?: (value: string) => void;
  [extension: string]: unknown;
};
export type NativeProps = Omit<WidgetProps, "onPress" | "onChange">;
export type NativeBridge = {
  create(kind: string, parent: number, props: NativeProps): number;
  update(id: number, props: NativeProps): void;
  remove(id: number): void;
  log(message: string): void;
  invoke(service: string, method: string, jsonArgs: string): string;
  stats(): Record<string, unknown>;
};
export type WidgetEvent = "press" | "change";
export type Cleanup = () => void;
export type Signal<T> = { value: T; subscribe(listener: (value: T) => void): Cleanup };

declare global {
  // Installed by the native host. These declarations also make the SDK usable in bundlers.
  var __cp: NativeBridge | undefined;
  var __cpDispatch: ((id: number, event: WidgetEvent, value?: string) => void) | undefined;
  var __cpTick: ((nowMs: number) => void) | undefined;
}

function native(): NativeBridge {
  if (!globalThis.__cp) throw new Error("CPZero native bridge is unavailable. Run this app in the CPZero host.");
  return globalThis.__cp;
}

type Handler = (value: string) => void;
function releaseCleanup(cleanup: Cleanup): void {
  try { cleanup(); }
  catch (error) { try { native().log(`cleanup failed: ${String(error)}`); } catch { /* Teardown must finish even when logging is unavailable. */ } }
}
type Timer = { id: number; due: number; interval: number; fn: () => void };
let nextTimerId = 1;

/** One running application owns its widget tree, subscriptions, timers, and cleanup hooks. */
export class App {
  readonly root: Widget;
  private readonly widgets = new Map<number, Widget>();
  private readonly cleanups = new Set<Cleanup>();
  private readonly timers = new Map<number, Timer>();
  private disposed = false;
  private lastTick = 0;

  constructor(options: { title?: string; setup(root: Widget): void }) {
    if (activeApp && !activeApp.isDisposed) throw new Error("Only one CPZero app can run at a time.");
    activeApp = this;
    try { this.root = this.makeWidget("screen", 0, { text: options.title }); }
    catch (error) { activeApp = undefined; throw error; }
    globalThis.__cpDispatch = (id, event, value) => activeApp?.dispatch(id, event, value);
    globalThis.__cpTick = (nowMs) => activeApp?.tick(nowMs);
    try { options.setup(this.root); }
    catch (error) { this.dispose(); throw error; }
  }

  get isDisposed(): boolean { return this.disposed; }
  /** Release the full tree and all SDK-owned resources. Safe to call more than once. */
  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.timers.clear();
    for (const cleanup of [...this.cleanups]) releaseCleanup(cleanup);
    this.cleanups.clear();
    // Descendants first, with no recursion or repeated tree walks.
    const nodes = [...this.widgets.values()];
    for (let i = nodes.length - 1; i >= 0; i--) {
      if (!nodes[i].isDisposed) nodes[i].disposeFromApp();
      native().remove(nodes[i].id);
    }
    this.widgets.clear();
    if (activeApp === this) {
      activeApp = undefined;
      globalThis.__cpDispatch = undefined;
      globalThis.__cpTick = undefined;
    }
  }

  /** Registers work to run once when this app is disposed. Returns an unregister function. */
  onCleanup(cleanup: Cleanup): Cleanup {
    this.assertLive();
    this.cleanups.add(cleanup);
    return () => this.cleanups.delete(cleanup);
  }

  every(ms: number, fn: () => void): Cleanup { return this.addTimer(ms, fn, ms); }
  after(ms: number, fn: () => void): Cleanup { return this.addTimer(ms, fn, 0); }

  private addTimer(ms: number, fn: () => void, interval: number): Cleanup {
    this.assertLive();
    if (!Number.isFinite(ms) || ms <= 0) throw new RangeError("Timer duration must be a finite number greater than zero.");
    const id = nextTimerId++;
    this.timers.set(id, { id, due: this.lastTick + ms, interval, fn });
    return () => this.timers.delete(id);
  }

  private tick(nowMs: number): void {
    if (this.disposed || !Number.isFinite(nowMs)) return;
    this.lastTick = Math.max(this.lastTick, nowMs);
    // Snapshot IDs so callbacks may safely cancel or add timers during dispatch.
    const dueIds: number[] = [];
    for (const timer of this.timers.values()) if (timer.due <= this.lastTick) dueIds.push(timer.id);
    for (const id of dueIds) {
      const timer = this.timers.get(id);
      if (!timer) continue;
      if (timer.interval === 0) this.timers.delete(id);
      else timer.due = this.lastTick + timer.interval;
      timer.fn();
    }
  }

  private makeWidget(kind: WidgetKind, parentId: number, props: WidgetProps): Widget {
    this.assertLive();
    const { onPress, onChange, ...nativeProps } = props;
    const id = native().create(kind, parentId, nativeProps);
    const widget = new Widget(this, id, kind);
    this.widgets.set(id, widget);
    widget.setPropHandler("press", onPress);
    widget.setPropHandler("change", onChange);
    return widget;
  }

  /** Access the widget factory. */
  create(parent: Widget, kind: WidgetKind, props: WidgetProps = {}): Widget {
    this.assertOwned(parent);
    const widget = this.makeWidget(kind, parent.id, props);
    parent.addChild(widget);
    return widget;
  }

  private dispatch(id: number, event: WidgetEvent, value?: string): void {
    const widget = this.widgets.get(id);
    if (widget && !widget.isDisposed) widget.dispatch(event, value);
  }

  /** Owns a subscription cleanup so widget disposal also detaches the signal observer. */
  own(widget: Widget, cleanup: Cleanup): Cleanup {
    if (widget.isDisposed) { cleanup(); return () => {}; }
    return widget.own(cleanup);
  }

  forget(widget: Widget): void { this.widgets.delete(widget.id); }
  assertOwned(widget: Widget): void {
    this.assertLive();
    if (widget.app !== this || widget.isDisposed) throw new Error("Widget is disposed or belongs to another app.");
  }
  assertLive(): void { if (this.disposed) throw new Error("CPZero app has been disposed."); }
}

let activeApp: App | undefined;

export class Widget {
  private readonly handlers = new Map<WidgetEvent, Set<Handler>>();
  private readonly cleanups = new Set<Cleanup>();
  private disposed = false;
  private readonly children = new Set<Widget>();
  private parent?: Widget;
  private readonly propHandlers = new Map<WidgetEvent, Cleanup>();
  constructor(readonly app: App, readonly id: number, readonly kind: WidgetKind) {}
  get isDisposed(): boolean { return this.disposed; }
  /** @internal */ addChild(child: Widget): void { this.children.add(child); child.parent = this; }

  /** @internal */ setPropHandler(event: WidgetEvent, callback?: Handler): void {
    this.propHandlers.get(event)?.();
    this.propHandlers.delete(event);
    if (callback) this.propHandlers.set(event, this.on(event, callback));
  }

  update(props: WidgetProps): this {
    this.app.assertOwned(this);
    const { onPress, onChange, ...nativeProps } = props;
    native().update(this.id, nativeProps);
    if (Object.hasOwn(props, "onPress")) this.setPropHandler("press", onPress);
    if (Object.hasOwn(props, "onChange")) this.setPropHandler("change", onChange);
    return this;
  }

  on(event: WidgetEvent, callback: Handler): Cleanup {
    this.app.assertOwned(this);
    let callbacks = this.handlers.get(event);
    if (!callbacks) this.handlers.set(event, callbacks = new Set());
    callbacks.add(callback);
    return () => { callbacks?.delete(callback); if (callbacks?.size === 0) this.handlers.delete(event); };
  }

  own(cleanup: Cleanup): Cleanup {
    if (this.disposed) { cleanup(); return () => {}; }
    this.cleanups.add(cleanup);
    return () => this.cleanups.delete(cleanup);
  }

  dispatch(event: WidgetEvent, value?: string): void {
    const callbacks = this.handlers.get(event);
    if (callbacks) for (const callback of [...callbacks]) callback(value ?? "");
  }

  dispose(): void {
    if (this.disposed) return;
    const stack: Widget[] = [this];
    const nodes: Widget[] = [];
    while (stack.length) {
      const node = stack.pop()!;
      if (node.isDisposed) continue;
      nodes.push(node);
      for (const child of node.children) stack.push(child);
    }
    for (let i = nodes.length - 1; i >= 0; i--) {
      const node = nodes[i];
      node.disposeFromApp();
      node.app.forget(node);
      native().remove(node.id);
    }
  }
  /** @internal App teardown path. */
  disposeFromApp(): void {
    if (this.disposed) return;
    this.disposed = true;
    for (const cleanup of [...this.cleanups]) releaseCleanup(cleanup);
    this.cleanups.clear();
    this.handlers.clear();
    this.propHandlers.clear();
    this.parent?.children.delete(this);
    this.parent = undefined;
    this.children.clear();
  }
}

export function createApp(options: { title?: string; setup(root: Widget): void }): App { return new App(options); }

/** Composition API for the native widgets available in v0.1. */
export const ui = {
  column(parent: Widget, props: WidgetProps = {}): Widget { return parent.app.create(parent, "column", props); },
  row(parent: Widget, props: WidgetProps = {}): Widget { return parent.app.create(parent, "row", props); },
  label(parent: Widget, props: WidgetProps = {}): Widget { return parent.app.create(parent, "label", props); },
  button(parent: Widget, props: WidgetProps = {}): Widget { return parent.app.create(parent, "button", props); },
  input(parent: Widget, props: WidgetProps = {}): Widget { return parent.app.create(parent, "input", props); },
  bar(parent: Widget, props: WidgetProps = {}): Widget { return parent.app.create(parent, "bar", props); },
  box(parent: Widget, props: WidgetProps = {}): Widget { return parent.app.create(parent, "box", props); },
  /** Extend host-supported widget kinds. Rendering still requires matching native support. */
  custom(parent: Widget, kind: string, props: WidgetProps = {}): Widget {
    if (!kind || ["screen", "column", "row", "label", "button", "input", "bar", "box"].includes(kind)) {
      throw new Error("ui.custom requires a non-empty custom kind; use the built-in factory for built-ins.");
    }
    return parent.app.create(parent, kind as WidgetKind, props);
  },
  pagedList<T>(parent: Widget, options: PagedListOptions<T>): PagedList<T> { return createPagedList(parent, options); }
};

export type PagedListOptions<T> = {
  items: readonly T[];
  /** Height of the rows viewport, in native pixels. */
  height: number;
  /** Fixed height of each rendered row, in native pixels. */
  rowHeight: number;
  renderRow(item: T, index: number, row: Widget): void;
  width?: number | "100%";
  gap?: number;
  color?: string;
};

/** A simple paged list. It creates native row widgets only for the current page. */
export class PagedList<T> {
  readonly widget: Widget;
  readonly pageSize: number;
  private items: readonly T[];
  private readonly rows: Widget;
  private readonly pageLabel: Widget;
  private readonly previous: Widget;
  private readonly next: Widget;
  private readonly renderRow: PagedListOptions<T>["renderRow"];
  private readonly rowHeight: number;
  private readonly rowWidgets = new Set<Widget>();
  private readonly stopTracking: Cleanup;
  private pageIndex = 0;
  private disposed = false;

  constructor(parent: Widget, options: PagedListOptions<T>) {
    if (!Number.isFinite(options.rowHeight) || options.rowHeight <= 0 || !Number.isFinite(options.height) || options.height < 0) {
      throw new RangeError("pagedList height must be non-negative and rowHeight must be greater than zero.");
    }
    this.pageSize = Math.floor(options.height / options.rowHeight);
    if (this.pageSize < 1) throw new RangeError("pagedList height must fit at least one row.");
    this.items = options.items;
    this.renderRow = options.renderRow;
    this.rowHeight = options.rowHeight;
    this.widget = ui.column(parent, { width: options.width ?? "100%", height: options.height + 22, padding: 0, gap: options.gap ?? 2 });
    this.rows = ui.column(this.widget, { width: "100%", height: options.height, padding: 0, gap: 0 });
    const controls = ui.row(this.widget, { width: "100%", height: 20, padding: 0, gap: 2 });
    this.previous = ui.button(controls, { text: "<", width: 30, height: 20, padding: 0, onPress: () => this.goToPage(this.pageIndex - 1) });
    this.pageLabel = ui.label(controls, { text: "", width: 0, grow: 1, height: 20, color: options.color ?? "#94a8bf" });
    this.next = ui.button(controls, { text: ">", width: 30, height: 20, padding: 0, onPress: () => this.goToPage(this.pageIndex + 1) });
    this.stopTracking = this.widget.own(() => { this.disposed = true; this.rowWidgets.clear(); });
    this.renderPage();
  }

  get page(): number { return this.pageIndex; }
  get pageCount(): number { return Math.ceil(this.items.length / this.pageSize); }
  get isDisposed(): boolean { return this.disposed; }

  setItems(items: readonly T[]): void {
    this.assertLive();
    this.items = items;
    this.pageIndex = this.pageCount === 0 ? 0 : Math.min(this.pageIndex, this.pageCount - 1);
    this.renderPage();
  }

  goToPage(page: number): void {
    this.assertLive();
    if (!Number.isInteger(page)) throw new RangeError("Page index must be an integer.");
    const target = this.pageCount === 0 ? 0 : Math.min(Math.max(page, 0), this.pageCount - 1);
    if (target === this.pageIndex && this.rowsChildrenLive()) return;
    this.pageIndex = target;
    this.renderPage();
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.stopTracking();
    this.widget.dispose();
    this.rowWidgets.clear();
  }

  private assertLive(): void { if (this.disposed || this.widget.isDisposed) throw new Error("pagedList has been disposed."); }
  private rowsChildrenLive(): boolean { return !this.rows.isDisposed; }
  private renderPage(): void {
    if (this.disposed) return;
    for (const child of this.rowWidgets) child.dispose();
    this.rowWidgets.clear();
    const start = this.pageIndex * this.pageSize;
    const end = Math.min(start + this.pageSize, this.items.length);
    for (let index = start; index < end; index++) {
      const row = ui.row(this.rows, { width: "100%", height: this.rowHeight, padding: 0 });
      this.rowWidgets.add(row);
      this.renderRow(this.items[index], index, row);
    }
    this.pageLabel.update({ text: this.pageCount === 0 ? "0 / 0" : `${this.pageIndex + 1} / ${this.pageCount}` });
    this.previous.update({ disabled: this.pageIndex === 0 || this.pageCount === 0 });
    this.next.update({ disabled: this.pageIndex >= this.pageCount - 1 || this.pageCount === 0 });
  }
}

function createPagedList<T>(parent: Widget, options: PagedListOptions<T>): PagedList<T> {
  return new PagedList(parent, options);
}

/** Lightweight reactive value. Notifications use a snapshot, so listeners can unsubscribe safely. */
export function signal<T>(initial: T): Signal<T> {
  let current = initial;
  const listeners = new Set<(value: T) => void>();
  return {
    get value() { return current; },
    set value(next: T) { if (Object.is(current, next)) return; current = next; for (const listener of [...listeners]) listener(next); },
    subscribe(listener) { listeners.add(listener); return () => listeners.delete(listener); }
  };
}

/** Bind a signal to one native property; widget disposal detaches the observer. */
export function bind<T extends string | number>(widget: Widget, property: string, source: Signal<T>): Cleanup {
  widget.update({ [property]: source.value });
  const unsubscribe = source.subscribe(value => { if (!widget.isDisposed) widget.update({ [property]: value }); });
  const release = widget.own(unsubscribe);
  return () => { unsubscribe(); release(); };
}

export function onCleanup(cleanup: Cleanup): Cleanup {
  if (!activeApp) throw new Error("onCleanup must be called while an app is running.");
  return activeApp.onCleanup(cleanup);
}
export function every(ms: number, fn: () => void): Cleanup {
  if (!activeApp) throw new Error("every must be called while an app is running.");
  return activeApp.every(ms, fn);
}
export function after(ms: number, fn: () => void): Cleanup {
  if (!activeApp) throw new Error("after must be called while an app is running.");
  return activeApp.after(ms, fn);
}

export type Service = object;
const adapters = new Map<string, Service>();
export const services = {
  register<T extends Service>(name: string, implementation: T): Cleanup {
    if (!name.trim()) throw new Error("Service name cannot be empty.");
    adapters.set(name, implementation);
    return () => { if (adapters.get(name) === implementation) adapters.delete(name); };
  },
  async call<T = unknown>(name: string, method: string, ...args: unknown[]): Promise<T> {
    const implementation = adapters.get(name);
    if (implementation) {
      if (!Object.hasOwn(implementation, method)) throw new Error(`Service '${name}' has no method '${method}'.`);
      const fn = (implementation as Record<string, unknown>)[method];
      if (typeof fn !== "function") throw new Error(`Service '${name}' has no method '${method}'.`);
      return await fn.apply(implementation, args) as T;
    }
    const result = native().invoke(name, method, JSON.stringify(args));
    const value: unknown = JSON.parse(result);
    return value as T;
  }
};

/** Built-in JSON storage adapter backed by the native host's validated storage service. */
export const storage = {
  get<T = unknown>(key: string): Promise<T | null> { return services.call<T | null>("storage", "get", key); },
  async set<T>(key: string, value: T): Promise<void> { await services.call("storage", "set", key, value); }
};

export const log = (message: string): void => native().log(String(message));
export const stats = (): Record<string, unknown> => native().stats();
