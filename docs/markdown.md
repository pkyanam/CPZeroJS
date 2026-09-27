# Bounded Markdown rendering

`@cpzero/core` provides `parseMarkdown(text, limits?)` for parsing and `markdown(parent, options)` for rendering a compact CommonMark-inspired subset with native widgets. It runs in the app runtime without a browser, DOM, network access, or external parser dependency.

```ts
import { markdown } from "@cpzero/core";

const message = markdown(transcript, {
  text: reply,
  fontSize: 10,
  maxChars: 6000,
  maxBlocks: 16,
  maxSpans: 96,
});

// Reuse the controller while appending streamed text:
message.setText(nextReply);
message.setFontSize(12);
onCleanup(() => message.dispose());
```

The returned `MarkdownView` exposes `widget`, a bounded `source` prefix, `setText(text)`, `setFontSize(size)`, and `dispose()`. The caller remains responsible for the original message string; the parser and controller retain only a prefix within the character cap. `parseMarkdown` returns `{ source, blocks, truncated }`; `source` is likewise only that bounded raw prefix. When input exceeds a limit, rendered output ends with `… [display truncated]` instead of silently implying that all content was shown.

## Supported syntax

Block syntax includes ATX headings (`#` through `######`), paragraphs, ordered and unordered lists, block quotes, horizontal rules, and fenced code blocks using backticks or tildes. Inline syntax includes `**bold**`, `*emphasis*`, backtick code, and `[label](destination)` links. Link labels receive an accent style; destinations are omitted and never opened. Escaped punctuation and underscores inside words are preserved. Unsupported Markdown markers remain ordinary text where practical.

Inline bold, emphasis, code, and link labels are styles on native rich text spans. Headings use the nearest supported font-size steps. Code and quote blocks use compact panels; rules use a native divider. Styling uses the native palette and does not interpret HTML. HTML comments are removed from displayed text, while other raw HTML appears as literal text. No text can create a webview, execute markup, or launch a URL.

## Limits and behavior

Hard maximums are 16,384 UTF-16 code units of source, 32 blocks, and 256 native spans for one render. The options `maxChars`, `maxBlocks`, and `maxSpans` can lower these ceilings. Limits include room for a visible truncation marker. The parser scans only the bounded prefix; long input beyond it is not retained by the controller.

The renderer reuses widgets for blocks that keep the same type at the same position and skips native updates for unchanged block content. Changed same-type blocks update their existing spans. When a block changes kind, the renderer rebuilds that block and the following suffix to preserve LVGL child order. `setFontSize` recomputes span styles without reparsing source. Dispose the view when the surrounding content is removed; parent/app disposal also disposes its widget subtree.

This subset is designed for bounded chat and status text, not full CommonMark compatibility. The native `richText` widget wraps spans at the available width; exact glyph coverage and layout depend on the configured LVGL fonts. Keep message-level limits below the SDK hard caps when rendering several replies at once.
