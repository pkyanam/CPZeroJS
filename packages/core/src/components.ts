import { keyboard, ui, type Widget, type WidgetProps } from './index';

export interface DisclosureOptions {
  title: string;
  expanded?: boolean;
  header?: WidgetProps;
  body?: WidgetProps;
  render(body: Widget): void;
  onToggle?(expanded: boolean): void;
}
export interface DisclosureController {
  readonly widget: Widget;
  readonly header: Widget;
  readonly expanded: boolean;
  setExpanded(expanded: boolean): void;
  setTitle(title: string): void;
  dispose(): void;
}

/** Native button disclosure with lazy body creation and disposal on collapse. */
export function disclosure(parent: Widget, options: DisclosureOptions): DisclosureController {
  const widget = ui.column(parent, { width: '100%', height: 'content', padding: 0, gap: 2 });
  let expanded = false;
  let disposed = false;
  let title = String(options.title);
  let body: Widget | undefined;
  const header = ui.button(widget, {
    width: '100%', height: 24, padding: 2, focusable: true,
    ...options.header,
    text: '',
    onPress: () => setExpanded(!expanded),
  });
  const showTitle = (): void => { header.update({ text: `${expanded ? '[-]' : '[+]'} ${title}` }); };

  const setExpanded = (value: boolean): void => {
    if (disposed || expanded === value) return;
    expanded = value;
    if (expanded) {
      body = ui.column(widget, { width: '100%', height: 'content', padding: 4, gap: 3, ...options.body });
      try { options.render(body); }
      catch (error) { body.dispose(); body = undefined; expanded = false; showTitle(); throw error; }
    } else if (body) {
      body.dispose(); body = undefined;
    }
    showTitle();
    options.onToggle?.(expanded);
  };
  const setTitle = (value: string): void => {
    if (disposed) return;
    title = String(value);
    showTitle();
  };
  showTitle();
  widget.own(() => { disposed = true; body = undefined; });
  if (options.expanded) setExpanded(true);
  return {
    widget, header,
    get expanded() { return expanded; },
    setExpanded,
    setTitle,
    dispose() { if (!disposed) widget.dispose(); },
  };
}

export interface FocusScopeOptions { initial?: Widget; onEscape?(): void }
export interface FocusScopeController { dispose(): void; release(): void }
type ScopeEntry = { container: Widget; onEscape?: () => void };
const focusScopes: ScopeEntry[] = [];

/** Keep keyboard focus inside a native container until the scope is released. */
export function focusScope(container: Widget, options: FocusScopeOptions = {}): FocusScopeController {
  const initial = options.initial;
  if (initial && initial.app !== container.app) throw new Error('Initial focus widget must belong to the scope app.');
  if (initial?.isDisposed) throw new Error('Initial focus widget is disposed.');
  container.command('trapFocus', initial?.id);
  let active = true;
  const entry: ScopeEntry = { container, onEscape: options.onEscape };
  focusScopes.push(entry);
  const removeEntry = (): void => {
    const index = focusScopes.lastIndexOf(entry);
    if (index >= 0) focusScopes.splice(index, 1);
  };
  const removeKeyboard = options.onEscape ? keyboard.on(event => {
    if (event.key !== 'Escape' || focusScopes[focusScopes.length - 1] !== entry) return false;
    options.onEscape?.();
    return true;
  }) : () => {};
  const release = (): void => {
    if (!active) return;
    if (focusScopes[focusScopes.length - 1] !== entry) throw new Error('Focus scopes must be released in nesting order.');
    if (!container.isDisposed) container.command('releaseFocus');
    active = false;
    removeKeyboard();
    removeEntry();
  };
  const unown = container.own(release);
  const dispose = (): void => {
    if (!active) return;
    release();
    unown();
  };
  return { dispose, release: dispose };
}
