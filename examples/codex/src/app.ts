import { createApp, ui, every, after, keyboard, processes, services, log, createTheme, defineRecipe, createZoom, markdown, disclosure, focusScope, type DisclosureController, type MarkdownView, type Widget } from '@cpzero/core';
import { CodexClient, type CodexEvent, type CodexItemEvent } from '@cpzero/codex';

// Native pixels, real Codex. No terminal scraping, webview, or duplicated credentials.
const theme = createTheme({
  colors: {
    background: '#181818', surface: '#2f2f2e', foreground: '#e6e6e6', mutedForeground: '#a0a0a0',
    primary: '#ececec', primaryForeground: '#181818', muted: '#363636', border: '#464647',
    accent: '#d8bb82', accentForeground: '#181818', focus: '#b5b5b5',
  },
  radius: { sm: 4, md: 6, lg: 8 },
});
const c = { bg: theme.colors.background, panel: theme.colors.surface, ink: theme.colors.foreground,
  dim: theme.colors.mutedForeground, primary: theme.colors.primary, line: theme.colors.border,
  warning: theme.colors.accent, composer: theme.colors.muted, focus: theme.colors.focus };
const compactButton = defineRecipe({ base: t => ({ height: 20, padding: 1, fontSize: 10, radius: t.radius.md,
  bg: t.colors.muted, color: t.colors.foreground, borderWidth: 1, borderColor: t.colors.border, focusColor: t.colors.focus }) });
const MAX_MESSAGES = 24;
const MAX_MESSAGE_CHARS = 6000;
type Message = { row: Widget; body: MarkdownView; text: string; dirty: boolean; itemId?: string };
type ToolCard = { disclosure: DisclosureController; row: Widget; button: Widget; body?: MarkdownView; text: string; title: string; expanded: boolean; dirty: boolean };
type Request = Extract<CodexEvent, { type: 'approval' | 'user-input' | 'server-request' }>;

export interface CodexAppOptions {
  /** Optional starting message, useful for a reproducible demo or integration test. */
  initialPrompt?: string;
  /** Alternate backend adapter; the default uses the installed CLI. */
  createClient?: (platform: { cwd: string; codexCommand: string }) => CodexClient;
}

export function createCodexApp(options: CodexAppOptions = {}) {
return createApp({ title: 'CPZeroJS / Codex', setup(root) {
  root.update({ bg: c.bg, color: c.ink, padding: 5, gap: 3, fontSize: 12, scroll: 'none' });
  const header = ui.row(root, { width: '100%', height: 21, padding: 0, gap: 4, align: 'center' });
  ui.text(header, { text: 'CODEX', color: c.primary, fontSize: 12 });
  const zoom = createZoom({ initial: 12, levels: [8, 10, 12, 14, 16] });
  const status = ui.text(header, { text: 'Connecting', color: c.dim, fontSize: 10, grow: 1, overflow: 'ellipsis' });
  const fresh = ui.button(header, { ...compactButton.resolve(theme), text: 'New', width: 43, disabled: true, onPress: () => { if (ready) void newChat(); else void start(); } });
  ui.divider(root, { bg: c.line });
  const viewport = ui.scroll(root, { width: '100%', grow: 1, height: 1, padding: 2, gap: 5, bg: c.bg });
  const composer = ui.row(root, { width: '100%', height: 26, padding: 0, gap: 4, align: 'center' });
  const input = ui.input(composer, { grow: 1, height: 26, value: '', placeholder: 'Message Codex...', maxLength: 4096, fontSize: 12,
    bg: c.composer, color: c.ink, paddingX: 6, paddingY: 4, radius: theme.radius.lg, focusColor: c.focus, borderWidth: 1, borderColor: c.line, onSubmit: () => void send() });
  const action = ui.button(composer, { text: 'Send', width: 43, height: 26, fontSize: 10, padding: 2, radius: theme.radius.lg, focusColor: c.focus, borderWidth: 1, borderColor: '#ffffff', bg: c.primary, color: c.bg, disabled: true,
    onPress: () => { if (busy) void cancel(); else void send(); } });
  const hint = ui.text(root, { text: 'Enter send  /  Tab tools  /  Esc stop', fontSize: 8, color: c.dim, height: 9 });

  const messages: Message[] = [];
  const rows: Widget[] = [];
  const messageItems = new Map<string, Message>();
  const toolItems = new Map<string, ToolCard>();
  function trimRows() {
    while (rows.length >= MAX_MESSAGES) {
      const row = rows.shift()!;
      const index = messages.findIndex(message => message.row === row);
      if (index >= 0) { const [message] = messages.splice(index, 1); if (message.itemId) messageItems.delete(message.itemId); }
      for (const [id, tool] of toolItems) if (tool.row === row) toolItems.delete(id);
      row.dispose();
    }
  }
  function renderText(parent: Widget, text: string): MarkdownView {
    return markdown(parent, { text, fontSize: zoom.value, color: c.ink, mutedColor: c.dim,
      accentColor: c.primary, codeBg: c.panel, maxChars: MAX_MESSAGE_CHARS, maxBlocks: 16, maxSpans: 96 });
  }
  root.own(zoom.subscribe(size => {
    for (const message of messages) message.body.setFontSize(size);
    for (const tool of toolItems.values()) tool.body?.setFontSize(size);
    input.update({ fontSize: Math.min(size, 14) }); dirty = true;
  }));
  let client: CodexClient | undefined;
  root.app.onCleanup(() => client?.dispose());
  let ready = false;
  let opening = false;
  let displayedThread: string | undefined;
  let activeTool: string | undefined;
  let busy = false;
  let dirty = false;
  let follow = true;
  let autoScroll = false;
  viewport.update({ onScroll: () => { if (!autoScroll) follow = false; } });
  let reply: Message | undefined;
  let dialog: Widget | undefined;
  let dialogScroll: Widget | undefined;
  let request: Request | undefined;
  const pendingRequests: Request[] = [];
  let composerFocused = false;
  function focusComposer() { composerFocused = true; activeTool = undefined; input.focus(); hint.update({ text: 'Enter send  /  Tab tools  /  Esc stop' }); }
  input.update({ onFocus: () => { composerFocused = true; activeTool = undefined; hint.update({ text: 'Enter send  /  Tab tools  /  Esc stop' }); }, onBlur: () => { composerFocused = false; } });

  let lastQuiet = '';
  function state(text: string) { lastQuiet = text; status.update({ text }); log(`codex: ${text}`); }
  function setBusy(value: boolean) {
    busy = value;
    action.update({ text: value ? 'Stop' : 'Send', disabled: !ready, bg: c.primary });
    fresh.update({ disabled: opening || !ready });
  }
  function add(role: string, text: string): Message {
    trimRows();
    const row = ui.panel(viewport, { height: 'content', bg: role === 'YOU' ? c.panel : c.bg, padding: 4, gap: 2, radius: theme.radius.md, borderWidth: role === 'YOU' ? 1 : 0, borderColor: c.line, scroll: 'none' });
    ui.text(row, { text: role, fontSize: 8, color: role === 'CODEX' ? c.primary : c.dim });
    const body = renderText(row, text);
    const message: Message = { row, body, text, dirty: false };
    rows.push(row);
    messages.push(message);
    follow = true;
    dirty = true;
    return message;
  }
  function agentMessage(id?: string): Message {
    if (id && messageItems.has(id)) return messageItems.get(id)!;
    let message = reply;
    if (!message || (message.itemId && message.itemId !== id) || message.row.isDisposed) message = add('CODEX', '');
    if (id) { message.itemId = id; messageItems.set(id, message); }
    reply = message;
    return message;
  }
  function handleItem(event: CodexItemEvent) {
    if (event.itemType === 'agentMessage') {
      const message = agentMessage(event.itemId);
      if (event.phase === 'completed' && event.output && !message.text) {
        message.text = event.output.slice(-MAX_MESSAGE_CHARS); message.dirty = true; dirty = true;
      }
      return;
    }
    if (['userMessage', 'reasoning'].includes(event.itemType)) return;
    const id = event.itemId ?? `${event.turnId ?? ''}:${event.itemType}`;
    let tool = toolItems.get(id);
    if (!tool) {
      trimRows();
      const card = disclosure(viewport, {
        title: 'Working',
        header: { height: 24, fontSize: 10, radius: theme.radius.md, focusColor: c.focus, bg: c.composer, color: c.ink, textAlign: 'left',
          onFocus: () => { activeTool = id; composerFocused = false; follow = false; hint.update({ text: 'Enter open / close  /  Up Down scroll  /  Esc back' }); },
          onBlur: () => { if (activeTool === id) activeTool = undefined; },
        },
        body: { padding: 3, bg: c.panel },
        render: container => { const current = toolItems.get(id); if (current) current.body = renderText(container, current.text); },
        onToggle: expanded => { const current = toolItems.get(id); if (current) { current.expanded = expanded; if (!expanded) current.body = undefined; follow = false; } },
      });
      const row = card.widget;
      row.update({ bg: c.panel, padding: 2, radius: theme.radius.md, borderWidth: 1, borderColor: c.line });
      tool = { disclosure: card, row, button: card.header, text: '', title: '', expanded: false, dirty: true };
      toolItems.set(id, tool); rows.push(row);
    }
    const label = event.summary || event.itemType;
    const state = event.phase === 'completed' ? (event.success === false ? 'Failed' : 'Done') : event.status || 'Working';
    tool.title = `${state}: ${label}`.slice(0, 110);
    if (event.detail) tool.text = event.detail.slice(0, MAX_MESSAGE_CHARS);
    if (event.output) {
      const output = event.output.slice(-3500);
      tool.text = event.phase === 'output' ? (tool.text + output).slice(-MAX_MESSAGE_CHARS)
        : (tool.text + '\n\n' + output).slice(-MAX_MESSAGE_CHARS);
    }
    tool.dirty = true; dirty = true;
  }
  function fail(error: unknown) {
    state('Check connection');
    setBusy(false);
    add('NOTICE', String(error instanceof Error ? error.message : error).slice(0, 800));
    if (!client?.isConnected) fresh.update({ text: 'Retry', disabled: false });
  }
  async function newChat() {
    if (!client || opening) return;
    const wasBusy = busy;
    opening = true; ready = false; displayedThread = undefined;
    // Release the entire old view before any network/process await.
    for (const pending of [request, ...pendingRequests]) if (pending) { try { client.deny(pending.id, 'New conversation started.'); } catch {} }
    pendingRequests.length = 0;
    dialog?.dispose(); dialog = undefined; dialogScroll = undefined; request = undefined;
    viewport.update({ hidden: false }); composer.update({ hidden: false }); hint.update({ hidden: false });
    for (const row of rows.splice(0)) row.dispose();
    messages.length = 0; messageItems.clear(); toolItems.clear();
    reply = undefined; activeTool = undefined;
    input.update({ value: '' }); viewport.scrollTo(0); follow = true;
    setBusy(false); state('Connecting'); focusComposer();
    try {
      if (wasBusy) await client.cancel();
      displayedThread = await client.newThread();
      ready = true; opening = false; setBusy(false); state('Ready'); focusComposer();
    } catch (error) { opening = false; fail(error); fresh.update({ text: 'Retry', disabled: false }); }
  }
  async function send() {
    if (!client || !ready || busy || request) return;
    const text = input.getValue().trim();
    if (!text) { focusComposer(); return; }
    input.update({ value: '' });
    add('YOU', text.slice(0, MAX_MESSAGE_CHARS));
    reply = add('CODEX', 'Thinking...'); reply.text = '';
    setBusy(true); state('Thinking'); focusComposer();
    const thread = displayedThread;
    try { await client.send(text); } catch (error) { if (thread === displayedThread) fail(error); }
  }
  async function cancel() {
    if (request) { resolveRequest(false); return; }
    if (!client || !busy) return;
    state('Stopping');
    try { await client.cancel(); } catch (error) { fail(error); }
  }
  function closeDialog() {
    dialog?.dispose(); dialog = undefined; dialogScroll = undefined; request = undefined;
    viewport.update({ hidden: false }); composer.update({ hidden: false });
    fresh.update({ disabled: !ready }); focusComposer(); hint.update({ hidden: false });
    if (pendingRequests.length) showRequest(pendingRequests.shift()!);
  }
  function resolveRequest(allow: boolean) {
    if (!client || !request) return;
    const current = request;
    if (current.type === 'approval' && /item\/(commandExecution|fileChange)\/requestApproval/.test(current.method)) {
      client.resolveApproval(current.id, allow ? 'accept' : 'decline');
    } else client.deny(current.id);
    closeDialog(); state(busy ? 'Working' : 'Ready');
  }
  function showRequest(event: Request) {
    if (request) {
      if (pendingRequests.length >= 8) client?.deny(event.id, 'Too many pending requests.');
      else pendingRequests.push(event);
      return;
    }
    request = event;
    viewport.update({ hidden: true }); composer.update({ hidden: true }); hint.update({ hidden: true });
    dialog = ui.panel(root, { width: '100%', height: 124, bg: c.panel, color: c.ink, padding: 5, radius: theme.radius.lg, borderWidth: 1, borderColor: c.line, gap: 3 });
    // A fixed dialog fits the native screen; details remain scrollable.
    const details = ui.scroll(dialog, { width: '100%', height: 78, gap: 2, color: c.ink });
    dialogScroll = details;
    const params = (event.params ?? {}) as Record<string, any>;
    ui.text(details, { text: event.type === 'user-input' ? 'CODEX NEEDS INPUT' : 'APPROVAL REQUIRED', fontSize: 10, color: c.warning });
    if (event.type === 'user-input' && Array.isArray(params.questions) && params.questions.length) {
      renderQuestion(details, dialog, event, params.questions.slice(0, 8));
      focusScope(dialog);
      return;
    }
    const supported = /item\/(commandExecution|fileChange)\/requestApproval/.test(event.method);
    ui.text(details, { text: `${String(params.command ?? toolItems.get(String(params.itemId))?.title ?? params.reason ?? 'Review this request')}\n${params.cwd ? 'In: ' + String(params.cwd) + '\n' : ''}${String(params.reason ?? '')}\n${JSON.stringify(params.changes ?? params.availableDecisions ?? {}, null, 2).slice(0, 3000)}`, width: '100%', fontSize: 10, color: c.ink });
    const buttons = ui.row(dialog, { height: 25, width: '100%', gap: 4, padding: 0 });
    const deny = ui.button(buttons, { text: 'Deny', width: 70, height: 24, radius: theme.radius.md, focusColor: c.focus, fontSize: 10, bg: c.composer, onPress: () => resolveRequest(false) });
    if (supported) ui.button(buttons, { text: 'Allow once', width: 90, height: 24, radius: theme.radius.md, focusColor: c.focus, fontSize: 10, bg: c.primary, color: c.bg, onPress: () => resolveRequest(true) });
    focusScope(dialog, { initial: deny }); state('Needs approval');
  }
  function renderQuestion(details: Widget, parent: Widget, event: Request, questions: any[]) {
    let index = 0;
    const answers: Record<string, { answers: string[] }> = {};
    let questionBody: Widget | undefined;
    const controls = ui.row(parent, { height: 25, width: '100%', gap: 4, padding: 0 });
    const field = ui.input(controls, { grow: 1, height: 22, fontSize: 10, maxLength: 1000, placeholder: 'Your answer', onSubmit: () => next(field.getValue()) });
    ui.button(controls, { text: 'Next', width: 40, height: 22, fontSize: 10, radius: theme.radius.md, focusColor: c.focus, onPress: () => next(field.getValue()) });
    ui.button(controls, { text: 'Cancel', width: 46, height: 22, fontSize: 10, radius: theme.radius.md, focusColor: c.focus, onPress: () => resolveRequest(false) });
    function next(value: string) {
      if (!value.trim()) return;
      answers[String(questions[index].id)] = { answers: [value.trim()] };
      if (++index >= questions.length) { client?.respond(event.id, { answers }); closeDialog(); state('Working'); }
      else draw();
    }
    function draw() {
      questionBody?.dispose();
      questionBody = ui.column(details, { width: '100%', height: 'content', gap: 3, padding: 0 });
      const question = questions[index];
      ui.text(questionBody, { text: String(question.question ?? question.header ?? 'Question'), width: '100%', fontSize: 10 });
      for (const option of (question.options ?? []).slice(0, 10)) {
        ui.button(questionBody, { text: String(option.label), width: '100%', height: 22, fontSize: 10, radius: theme.radius.md, focusColor: c.focus, onPress: () => next(String(option.label)) });
      }
      field.update({ value: '' }); field.focus(); details.scrollTo(0);
    }
    draw(); state('Needs input');
  }

  every(33, () => {
    if (!dirty) return;
    dirty = false;
    for (const message of messages) if (message.dirty) { message.body.setText(message.text || 'Thinking...'); message.dirty = false; }
    for (const tool of toolItems.values()) if (tool.dirty) {
      tool.disclosure.setTitle(tool.title);
      if (tool.expanded) tool.body?.setText(tool.text);
      tool.dirty = false;
    }
    if (follow && !request) {
      autoScroll = true;
      try { viewport.scrollToEnd(); } finally { autoScroll = false; }
    }
  });
  keyboard.on(event => {
    try { composerFocused = input.isFocused(); } catch { /* Older simulators use focus events. */ }
    if ((event.ctrl || event.meta) && ['+', '=', '-', '0'].includes(event.key)) {
      if (event.key === '-') zoom.zoomOut(); else if (event.key === '0') zoom.reset(); else zoom.zoomIn();
      return true;
    }
    if (event.key === 'Escape') {
      if (request || busy) void cancel(); else focusComposer();
      return true;
    }
    if ((event.ctrl || event.meta) && event.key.toLowerCase() === 'n') { void newChat(); return true; }
    if (request && ['PageUp', 'PageDown', 'ArrowUp', 'ArrowDown'].includes(event.key)) {
      dialogScroll?.scrollBy(event.key === 'PageUp' ? -60 : event.key === 'PageDown' ? 60 : event.key === 'ArrowUp' ? -24 : 24); return true;
    }
    if (!request && (event.ctrl || event.meta) && (event.key === 'ArrowUp' || event.key === 'ArrowDown')) {
      const ids = [...toolItems.keys()];
      if (ids.length) {
        const index = activeTool ? ids.indexOf(activeTool) : event.key === 'ArrowUp' ? ids.length : -1;
        activeTool = ids[Math.max(0, Math.min(ids.length - 1, index + (event.key === 'ArrowUp' ? -1 : 1)))];
        composerFocused = false; follow = false; toolItems.get(activeTool)?.button.focus();
        hint.update({ text: 'Enter open / close  /  Up Down scroll  /  Esc back' });
      }
      return true;
    }
    if (!request && (event.key === 'PageUp' || event.key === 'PageDown')) {
      follow = false; viewport.scrollBy(event.key === 'PageUp' ? -70 : 70); return true;
    }
    if (!request && (event.key === 'ArrowUp' || event.key === 'ArrowDown') && (event.alt || !composerFocused)) {
      follow = false; viewport.scrollBy(event.key === 'ArrowUp' ? -32 : 32); return true;
    }
    if (!request && activeTool && (event.key === 'ArrowLeft' || event.key === 'ArrowRight')) {
      const tool = toolItems.get(activeTool);
      if (tool) { tool.disclosure.setExpanded(event.key === 'ArrowRight'); follow = false; }
      return true;
    }
    if (!request && (event.ctrl || event.meta) && event.key === 'End') { follow = true; viewport.scrollToEnd(); return true; }
    if (!request && !composerFocused && event.key === '/') { focusComposer(); return true; }
    return false;
  });
  add('CODEX', 'Connecting to your installed Codex CLI...');
  after(1, () => { void start(); });
  async function start() {
    fresh.update({ text: 'New', disabled: true });
    try {
      client?.dispose();
      state('Connecting');
      const platform = await services.call<{ cwd: string; codexCommand: string }>('platform', 'get');
      client = options.createClient?.(platform) ?? new CodexClient({ spawn: options => processes.spawn(options), command: platform.codexCommand, cwd: platform.cwd, maxTranscriptChars: 8192 });
      client.on(event => {
        const eventThread = 'threadId' in event ? event.threadId : 'params' in event ? (event.params as any)?.threadId : undefined;
        if (eventThread && eventThread !== displayedThread) return;
        if (opening && ['delta', 'item', 'activity', 'error'].includes(event.type)) return;
        if (opening && (event.type === 'approval' || event.type === 'user-input' || event.type === 'server-request')) { client?.deny(event.id, 'Conversation replaced.'); return; }
        if (event.type === 'delta') {
          const message = agentMessage(event.itemId);
          message.text = (message.text + event.text).slice(-MAX_MESSAGE_CHARS);
          message.dirty = true; dirty = true; stateQuiet('Writing');
        } else if (event.type === 'item') handleItem(event);
        else if (event.type === 'activity') {
          if (event.method === 'turn/completed') {
            setBusy(false); state('Ready');
            if (reply && !reply.text) { reply.text = 'Turn finished.'; reply.dirty = true; dirty = true; }
          } else if (event.method === 'item/started') stateQuiet('Working');
        } else if (event.type === 'approval' || event.type === 'user-input' || event.type === 'server-request') showRequest(event);
        else if (event.type === 'error') fail(event.error);
        else if (event.type === 'closed') { ready = false; setBusy(false); state('Disconnected'); fresh.update({ text: 'Retry', disabled: false }); }
      });
      await client.connect();
      await newChat();
      if (options.initialPrompt && ready) { input.update({ value: options.initialPrompt.slice(0, 4096) }); await send(); }
    } catch (error) { fail(error); }
  }
  function stateQuiet(text: string) { if (lastQuiet !== text) { lastQuiet = text; status.update({ text }); } }
}});
}
