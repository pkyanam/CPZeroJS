import { ui, type Widget, type WidgetKind } from './index';

const HARD_MAX_CHARS = 16_384;
const HARD_MAX_BLOCKS = 32;
const HARD_MAX_SPANS = 256;
const TRUNCATION_TEXT = '… [display truncated]';
const FONT_SIZES = [8, 10, 12, 14, 16, 20] as const;

type Emphasis = 'bold' | 'italic' | 'code' | 'link';
export interface MarkdownSpan { text: string; emphasis?: Emphasis }
export type MarkdownBlock =
  | { type: 'heading'; level: number; spans: MarkdownSpan[] }
  | { type: 'paragraph'; spans: MarkdownSpan[] }
  | { type: 'list'; ordered: boolean; start: number; items: MarkdownSpan[][] }
  | { type: 'quote'; spans: MarkdownSpan[] }
  | { type: 'code'; text: string }
  | { type: 'rule' }
  | { type: 'truncated'; spans: MarkdownSpan[] };
export interface MarkdownDocument { source: string; blocks: MarkdownBlock[]; truncated: boolean }
export interface MarkdownLimits { maxChars?: number; maxBlocks?: number; maxSpans?: number }
export interface MarkdownOptions extends MarkdownLimits {
  text: string;
  fontSize?: number;
  color?: string;
  mutedColor?: string;
  accentColor?: string;
  codeBg?: string;
}
export interface MarkdownView {
  /** Container holding the rendered markdown blocks. */
  readonly widget: Widget;
  /** Bounded raw source prefix currently being displayed. */
  readonly source: string;
  /** Replace content, reusing block widgets by position and kind where possible. */
  setText(text: string): void;
  /** Update base size and recompute the limited heading/emphasis size ramp. */
  setFontSize(size: number): void;
  dispose(): void;
}

type InlineStyle = { bold: boolean; italic: boolean; code: boolean; link: boolean };
type InlineResult = { spans: MarkdownSpan[]; overflow: boolean };
type Line = { text: string; next: number };

function boundedLimit(value: number | undefined, fallback: number, hardMax: number): number {
  if (value === undefined || !Number.isFinite(value)) return fallback;
  return Math.max(1, Math.min(hardMax, Math.floor(value)));
}
function cappedPrefix(value: string, maxChars: number): string {
  let end = Math.min(value.length, maxChars);
  const last = value.charCodeAt(end - 1);
  if (end < value.length && last >= 0xd800 && last <= 0xdbff) end--;
  return value.slice(0, end);
}
function isNewline(ch: string): boolean { return ch === '\n' || ch === '\r'; }
function readLine(text: string, start: number): Line {
  let end = start;
  while (end < text.length && !isNewline(text[end])) end++;
  let next = end;
  if (text[next] === '\r' && text[next + 1] === '\n') next += 2;
  else if (next < text.length) next++;
  return { text: text.slice(start, end), next };
}
function sanitizeComments(text: string): string {
  const out: string[] = [];
  let i = 0;
  let comment = false;
  while (i < text.length) {
    if (!comment && text[i] === '<' && text[i + 1] === '!' && text[i + 2] === '-' && text[i + 3] === '-') {
      comment = true; i += 4; continue;
    }
    if (comment) {
      if (text[i] === '-' && text[i + 1] === '-' && text[i + 2] === '>') { comment = false; i += 3; }
      else { if (isNewline(text[i])) out.push(text[i]); i++; }
      continue;
    }
    out.push(text[i++]);
  }
  return out.join('');
}
function markerAt(text: string, index: number, marker: string): boolean {
  if (text[index] !== marker[0]) return false;
  for (let j = 1; j < marker.length; j++) if (text[index + j] !== marker[j]) return false;
  return true;
}
function parseInline(text: string, limit: number): InlineResult {
  const spans: MarkdownSpan[] = [];
  const active = { bold: false, italic: false, code: false, link: false };
  let i = 0, plainStart = 0, linkStart = -1, linksDisabled = false, overflow = false;
  const style = (): Emphasis | undefined => active.code ? 'code' : active.link ? 'link' : active.bold ? 'bold' : active.italic ? 'italic' : undefined;
  const emit = (value: string, emphasis = style()): void => {
    if (!value) return;
    if (spans.length < limit) spans.push(emphasis ? { text: value, emphasis } : { text: value });
    else overflow = true;
  };
  const flush = (end: number): void => { if (end > plainStart) emit(text.slice(plainStart, end)); };
  while (i < text.length && !overflow) {
    if (!active.code && !linksDisabled && text[i] === '[') { linkStart = i; i++; continue; }
    if (!active.code && !linksDisabled && linkStart >= 0 && text[i] === ']' && text[i + 1] === '(') {
      let close = i + 2;
      while (close < text.length && text[close] !== ')') close++;
      if (close === text.length) {
        // A malformed link disables further lookahead, keeping adversarial input linear.
        linksDisabled = true; linkStart = -1; i += 2; continue;
      }
      flush(linkStart);
      emit(text.slice(linkStart + 1, i), 'link');
      i = close + 1; plainStart = i; linkStart = -1; continue;
    }
    if (!active.code && text[i] === '\\' && i + 1 < text.length && /[\\`*_{}\[\]()#+.!>~-]/.test(text[i + 1])) {
      flush(i); emit(text[i + 1]); i += 2; plainStart = i; linkStart = -1; continue;
    }
    if (text[i] === '`') {
      flush(i);
      active.code = !active.code;
      i++; plainStart = i; linkStart = -1; continue;
    }
    if (!active.code) {
      const pair = text[i] === '*' || text[i] === '_' ? text[i] + text[i] : '';
      if (pair && markerAt(text, i, pair)) {
        flush(i); active.bold = !active.bold; i += 2; plainStart = i; linkStart = -1; continue;
      }
      if (text[i] === '*' || text[i] === '_') {
        const before = i > 0 ? text[i - 1] : '';
        const after = text[i + 1] ?? '';
        const word = (ch: string): boolean => (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        const intrawordUnderscore = text[i] === '_' && word(before) && word(after);
        const canOpen = after !== '' && after !== ' ' && after !== '\t';
        const canClose = before !== '' && before !== ' ' && before !== '\t';
        if (!intrawordUnderscore && (active.italic ? canClose : canOpen)) {
          flush(i); active.italic = !active.italic; i++; plainStart = i; linkStart = -1; continue;
        }
      }
    }
    i++;
  }
  if (!overflow) flush(text.length);
  return { spans, overflow };
}
function parseFence(line: string): { marker: string; length: number } | undefined {
  let i = 0;
  while (i < line.length && i < 3 && line[i] === ' ') i++;
  const ch = line[i];
  if (ch !== '`' && ch !== '~') return undefined;
  let end = i;
  while (line[end] === ch) end++;
  return end - i >= 3 ? { marker: ch, length: end - i } : undefined;
}
function isFenceClose(line: string, fence: { marker: string; length: number }): boolean {
  let i = 0;
  while (i < line.length && i < 3 && line[i] === ' ') i++;
  let end = i;
  while (line[end] === fence.marker) end++;
  if (end - i < fence.length) return false;
  while (end < line.length) { if (line[end] !== ' ' && line[end] !== '\t') return false; end++; }
  return true;
}
function heading(line: string): { level: number; text: string } | undefined {
  let i = 0;
  while (i < line.length && i < 3 && line[i] === ' ') i++;
  let end = i;
  while (line[end] === '#' && end - i < 6) end++;
  const count = end - i;
  if (count && (end === line.length || line[end] === ' ' || line[end] === '\t')) return { level: count, text: line.slice(end).trim() };
  return undefined;
}
function isRule(line: string): boolean {
  const value = line.trim();
  if (value.length < 3) return false;
  const mark = value[0];
  if (mark !== '-' && mark !== '*' && mark !== '_') return false;
  for (let i = 1; i < value.length; i++) if (value[i] !== mark && value[i] !== ' ') return false;
  let count = 0; for (let i = 0; i < value.length; i++) if (value[i] === mark) count++;
  return count >= 3;
}
function listMarker(line: string): { ordered: boolean; start: number; content: string } | undefined {
  let i = 0;
  while (i < line.length && i < 3 && line[i] === ' ') i++;
  if (line[i] === '-' || line[i] === '+' || line[i] === '*') {
    if (line[i + 1] === ' ' || line[i + 1] === '\t') return { ordered: false, start: 1, content: line.slice(i + 2) };
    return undefined;
  }
  const digitStart = i;
  while (line[i] >= '0' && line[i] <= '9') i++;
  if (i === digitStart || (line[i] !== '.' && line[i] !== ')') || (line[i + 1] !== ' ' && line[i + 1] !== '\t')) return undefined;
  const value = Number(line.slice(digitStart, i));
  return { ordered: true, start: Number.isSafeInteger(value) && value > 0 ? value : 1, content: line.slice(i + 2) };
}
function isQuote(line: string): boolean { return line.startsWith('>') || (line.startsWith(' ') && line.trimStart().startsWith('>')); }
function quoteContent(line: string): string {
  let i = 0; while (i < line.length && i < 3 && line[i] === ' ') i++;
  if (line[i] === '>') i++;
  if (line[i] === ' ') i++;
  return line.slice(i);
}

/** Parse the safe, bounded CommonMark-inspired subset rendered by CPZero. */
export function parseMarkdown(input: string, limits: MarkdownLimits = {}): MarkdownDocument {
  const maxChars = boundedLimit(limits.maxChars, HARD_MAX_CHARS, HARD_MAX_CHARS);
  const maxBlocks = boundedLimit(limits.maxBlocks, HARD_MAX_BLOCKS, HARD_MAX_BLOCKS);
  const maxSpans = boundedLimit(limits.maxSpans, HARD_MAX_SPANS, HARD_MAX_SPANS);
  const source = cappedPrefix(String(input), maxChars);
  const charTruncated = source.length < String(input).length;
  const text = sanitizeComments(source);
  const contentBlockLimit = Math.max(0, maxBlocks - 1);
  const spanContentLimit = Math.max(0, maxSpans - 1);
  const blocks: MarkdownBlock[] = [];
  let spanCount = 0, truncated = charTruncated, overflow = false, cursor = 0;

  const inline = (value: string, extraSpans = 0): MarkdownSpan[] => {
    if (spanCount + extraSpans > spanContentLimit) { overflow = true; truncated = true; return []; }
    const room = Math.max(0, spanContentLimit - spanCount - extraSpans);
    const parsed = parseInline(value, room);
    spanCount += extraSpans + parsed.spans.length;
    if (parsed.overflow) { overflow = true; truncated = true; }
    return parsed.spans;
  };
  const add = (block: MarkdownBlock): void => {
    if (blocks.length >= contentBlockLimit) { truncated = true; overflow = true; return; }
    blocks.push(block);
  };
  while (cursor < text.length && !overflow) {
    const current = readLine(text, cursor);
    const raw = current.text;
    const trimmed = raw.trim();
    if (!trimmed) { cursor = current.next; continue; }

    const fence = parseFence(raw);
    if (fence) {
      if (spanCount >= spanContentLimit) { truncated = true; overflow = true; break; }
      spanCount++;
      cursor = current.next;
      const code: string[] = [];
      while (cursor < text.length) {
        const line = readLine(text, cursor); cursor = line.next;
        if (isFenceClose(line.text, fence)) break;
        code.push(line.text);
      }
      add({ type: 'code', text: code.join('\n') });
      continue;
    }
    const header = heading(raw);
    if (header) { add({ type: 'heading', level: header.level, spans: inline(header.text) }); cursor = current.next; continue; }
    if (isRule(raw)) { add({ type: 'rule' }); cursor = current.next; continue; }
    if (isQuote(raw)) {
      const parts: string[] = [];
      while (cursor < text.length) {
        const line = readLine(text, cursor);
        if (!isQuote(line.text)) break;
        parts.push(quoteContent(line.text)); cursor = line.next;
      }
      add({ type: 'quote', spans: inline(parts.join('\n'), 1) });
      continue;
    }
    const list = listMarker(raw);
    if (list) {
      const items: MarkdownSpan[][] = [];
      let first = list.start;
      while (cursor < text.length) {
        const itemStart = cursor;
        let probe = cursor;
        while (probe < text.length) {
          const blank = readLine(text, probe);
          if (blank.text.trim()) break;
          probe = blank.next;
        }
        const line = readLine(text, probe);
        const marker = listMarker(line.text);
        if (!marker || marker.ordered !== list.ordered) { cursor = itemStart; break; }
        if (items.length === 0) first = marker.start;
        const spans = inline(marker.content, 1);
        cursor = line.next;
        if (overflow) break;
        items.push(spans);
      }
      add({ type: 'list', ordered: list.ordered, start: first, items });
      continue;
    }
    const parts = [raw]; cursor = current.next;
    while (cursor < text.length) {
      const line = readLine(text, cursor);
      if (!line.text.trim() || parseFence(line.text) || heading(line.text) || isRule(line.text) || isQuote(line.text) || listMarker(line.text)) break;
      parts.push(line.text); cursor = line.next;
    }
    add({ type: 'paragraph', spans: inline(parts.join(' ')) });
  }
  if (!overflow && cursor < text.length && text.slice(cursor).trim()) truncated = true;
  if (truncated) blocks.push({ type: 'truncated', spans: [{ text: TRUNCATION_TEXT, emphasis: 'italic' }] });
  return { source, blocks, truncated };
}

function normalizeFontSize(size: number): number {
  const requested = Number.isFinite(size) ? size : 12;
  let nearest: number = FONT_SIZES[0];
  for (const candidate of FONT_SIZES) if (Math.abs(candidate - requested) < Math.abs(nearest - requested)) nearest = candidate;
  return nearest;
}
function fontStep(size: number, steps: number): number {
  const index = FONT_SIZES.indexOf(normalizeFontSize(size) as typeof FONT_SIZES[number]);
  return FONT_SIZES[Math.min(FONT_SIZES.length - 1, index + steps)];
}
function listSpans(block: Extract<MarkdownBlock, { type: 'list' }>): MarkdownSpan[] {
  const out: MarkdownSpan[] = [];
  for (let i = 0; i < block.items.length; i++) {
    const item = block.items[i];
    const prefix = `${i ? '\n' : ''}${block.ordered ? `${block.start + i}. ` : '• '}`;
    if (item[0] && !item[0].emphasis) {
      out.push({ text: prefix + item[0].text });
      for (let j = 1; j < item.length; j++) out.push(item[j]);
    } else {
      out.push({ text: prefix });
      for (const span of item) out.push(span);
    }
  }
  return out;
}
function renderedSpans(block: MarkdownBlock, baseSize: number, colors: { color: string; muted: string; accent: string }): Array<{ text: string; color?: string; fontSize?: number }> {
  if (block.type === 'rule') return [];
  let spans: MarkdownSpan[];
  if (block.type === 'code') spans = [{ text: block.text, emphasis: 'code' }];
  else if (block.type === 'list') spans = listSpans(block);
  else if (block.type === 'quote') spans = block.spans;
  else spans = block.spans;
  return spans.map(span => {
    const fontSize = block.type === 'heading' ? fontStep(baseSize, Math.max(0, 3 - block.level)) : span.emphasis === 'bold' ? fontStep(baseSize, 1) : span.emphasis === 'code' ? normalizeFontSize(baseSize - (baseSize > 8 ? 2 : 0)) : normalizeFontSize(baseSize);
    const color = span.emphasis === 'link' ? colors.accent : span.emphasis === 'italic' || span.emphasis === 'code' || block.type === 'truncated' ? colors.muted : span.emphasis === 'bold' || block.type === 'heading' ? colors.accent : colors.color;
    return { text: span.text, color, fontSize };
  });
}
type View = { kind: MarkdownBlock['type']; signature: string; widget: Widget; content?: Widget; update(block: MarkdownBlock, size: number): void; dispose(): void };

/** Render bounded Markdown blocks as native rich text and compact layout widgets. */
export function markdown(parent: Widget, options: MarkdownOptions): MarkdownView {
  const colors = { color: options.color ?? '#e8f0fa', muted: options.mutedColor ?? '#91a4b9', accent: options.accentColor ?? '#79dfc1' };
  const codeBg = options.codeBg ?? '#152232';
  const limits: MarkdownLimits = { maxChars: options.maxChars, maxBlocks: options.maxBlocks, maxSpans: options.maxSpans };
  let baseSize = normalizeFontSize(options.fontSize ?? 12);
  let document = parseMarkdown(options.text, limits);
  let disposed = false;
  const widget = ui.column(parent, { width: '100%', height: 'content', padding: 0, gap: 3 });
  const views: View[] = [];
  const signature = (block: MarkdownBlock): string => JSON.stringify(block);

  const makeRich = (target: Widget, block: MarkdownBlock, size: number): Widget => target.app.create(target, 'richText' as WidgetKind, {
    width: '100%', height: 'content', padding: 0, color: colors.color, fontSize: size, overflow: 'wrap',
    spans: renderedSpans(block, size, colors),
  });
  const createView = (block: MarkdownBlock): View => {
    if (block.type === 'rule') {
      const rule = ui.divider(widget, { bg: colors.muted, height: 1, padding: 0 });
      return { kind: block.type, signature: signature(block), widget: rule, update(next) { this.signature = signature(next); }, dispose: () => rule.dispose() };
    }
    if (block.type === 'code' || block.type === 'quote') {
      const panel = ui.panel(widget, { width: '100%', height: 'content', padding: 3, gap: 0, bg: block.type === 'code' ? codeBg : '#101925', borderWidth: block.type === 'quote' ? 1 : 0, borderColor: colors.muted });
      const content = makeRich(panel, block, baseSize);
      return {
        kind: block.type, signature: signature(block), widget: panel, content,
        update(next, size) { this.signature = signature(next); panel.update({ bg: next.type === 'code' ? codeBg : '#101925', borderWidth: next.type === 'quote' ? 1 : 0, borderColor: colors.muted }); content.update({ spans: renderedSpans(next, size, colors), fontSize: size }); },
        dispose: () => panel.dispose(),
      };
    }
    const rich = makeRich(widget, block, baseSize);
    return {
      kind: block.type, signature: signature(block), widget: rich,
      update(next, size) { this.signature = signature(next); rich.update({ spans: renderedSpans(next, size, colors), fontSize: size }); },
      dispose: () => rich.dispose(),
    };
  };
  const render = (): void => {
    const nextBlocks = document.blocks;
    const sharedLength = Math.min(views.length, nextBlocks.length);
    let rebuildFrom = -1;
    for (let i = 0; i < sharedLength; i++) {
      if (views[i].kind !== nextBlocks[i].type) { rebuildFrom = i; break; }
    }
    if (rebuildFrom >= 0) {
      while (views.length > rebuildFrom) views.pop()!.dispose();
    }
    for (let i = 0; i < nextBlocks.length; i++) {
      const block = nextBlocks[i];
      const old = views[i];
      if (old && old.kind === block.type) { if (old.signature !== signature(block)) old.update(block, baseSize); }
      else {
        old?.dispose();
        views[i] = createView(block);
      }
    }
    while (views.length > nextBlocks.length) views.pop()!.dispose();
  };
  render();
  widget.own(() => { disposed = true; views.length = 0; });
  return {
    widget,
    get source() { return document.source; },
    setText(text) {
      if (disposed) return;
      document = parseMarkdown(text, limits);
      render();
    },
    setFontSize(size) {
      if (disposed) return;
      baseSize = normalizeFontSize(size);
      for (let i = 0; i < document.blocks.length; i++) views[i]?.update(document.blocks[i], baseSize);
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      widget.dispose(); views.length = 0;
    },
  };
}
