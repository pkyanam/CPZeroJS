import test from "node:test";
import assert from "node:assert/strict";
import { createApp, type NativeBridge } from "../packages/core/src/index.js";
import { markdown, parseMarkdown, type MarkdownBlock } from "../packages/core/src/markdown.js";

function visibleSpans(block: MarkdownBlock): number {
  if (block.type === "rule") return 0;
  if (block.type === "code") return 1;
  if (block.type === "list") return block.items.reduce((total, item) => total + item.length + 1, 0);
  return block.spans.length;
}
function mockHost() {
  let nextId = 1;
  const created: Array<{ id: number; kind: string; parent: number; props: Record<string, unknown> }> = [];
  const updated: Array<{ id: number; props: Record<string, unknown> }> = [];
  const removed: number[] = [];
  globalThis.__cp = {
    create(kind, parent, props) { const id = nextId++; created.push({ id, kind, parent, props }); return id; },
    update(id, props) { updated.push({ id, props }); }, remove(id) { removed.push(id); },
    invoke() { return "null"; }, log() {}, stats() { return {}; }, command() {},
  } satisfies NativeBridge;
  return { created, updated, removed };
}

function collectText(block: MarkdownBlock): string {
  if (block.type === "rule") return "";
  if (block.type === "code") return block.text;
  if (block.type === "list") return block.items.flat().map(span => span.text).join(" ");
  return block.spans.map(span => span.text).join("");
}

test("parses safe block and inline subset without interpreting HTML or activating links", () => {
  const doc = parseMarkdown([
    "# A **bold** heading",
    "A *small* word, `code`, and [label](https://example.test/path).",
    "- one",
    "- **two**",
    "> quoted text",
    "---",
    "```ts",
    "<script>literal</script>",
    "```",
    "<!-- hidden comment -->",
  ].join("\n\n"));
  assert.deepEqual(doc.blocks.map(block => block.type), ["heading", "paragraph", "list", "quote", "rule", "code"]);
  const heading = doc.blocks[0];
  assert.equal(heading.type, "heading");
  if (heading.type === "heading") assert.ok(heading.spans.some(span => span.text === "bold" && span.emphasis === "bold"));
  const paragraph = doc.blocks[1];
  assert.equal(paragraph.type, "paragraph");
  if (paragraph.type === "paragraph") {
    assert.ok(paragraph.spans.some(span => span.text === "small" && span.emphasis === "italic"));
    assert.ok(paragraph.spans.some(span => span.text === "code" && span.emphasis === "code"));
    assert.ok(paragraph.spans.some(span => span.text === "label" && span.emphasis === "link"));
    assert.ok(!collectText(paragraph).includes("https://"), "link destinations are never emitted or launched");
  }
  assert.ok(collectText(doc.blocks[5]).includes("<script>literal</script>"), "raw HTML remains plain text");
  assert.ok(!doc.blocks.some(collectTextPart => collectText(collectTextPart).includes("hidden comment")));
});

test("inline code, intraword underscores, and escapes stay literal", () => {
  const doc = parseMarkdown("foo_bar and `a_[b](url)_*` plus \\*literal\\*");
  assert.equal(doc.blocks[0].type, "paragraph");
  const spans = doc.blocks[0].type === "paragraph" ? doc.blocks[0].spans : [];
  assert.ok(spans.some(span => span.text.includes("foo_bar")));
  assert.ok(spans.some(span => span.text === "a_[b](url)_*" && span.emphasis === "code"));
  assert.equal(spans.slice(-3).map(span => span.text).join(""), "*literal*");
  assert.ok(spans.slice(-3).every(span => span.emphasis === undefined));
});

test("hard character, block, and span caps include a visible truncation marker", () => {
  const source = Array.from({ length: 20 }, (_, i) => `- item **${i}**`).join("\n") + "\n\n" + "z".repeat(1000);
  const doc = parseMarkdown(source, { maxChars: 100, maxBlocks: 4, maxSpans: 7 });
  assert.equal(doc.source.length, 100);
  assert.equal(doc.truncated, true);
  assert.ok(doc.blocks.length <= 4);
  assert.equal(doc.blocks.at(-1)?.type, "truncated");
  assert.ok(doc.blocks.reduce((sum, block) => sum + visibleSpans(block), 0) <= 7);
  assert.ok(doc.blocks.some(block => collectText(block).includes("display truncated")));
});

test("adversarial delimiter runs and an unclosed code fence remain bounded", () => {
  const hostile = "**".repeat(6000) + "_".repeat(6000) + "[".repeat(5000) + "\n```\n" + "x".repeat(6000);
  const doc = parseMarkdown(hostile, { maxChars: 4096, maxBlocks: 5, maxSpans: 20 });
  assert.ok(doc.source.length <= 4096);
  assert.equal(doc.truncated, true);
  assert.ok(doc.blocks.length <= 5);
  assert.ok(doc.blocks.reduce((sum, block) => sum + visibleSpans(block), 0) <= 20);
});

test("renderer reuses unchanged blocks and updates only changed rich text", () => {
  const host = mockHost();
  let view!: ReturnType<typeof markdown>;
  const app = createApp({ setup(root) {
    view = markdown(root, { text: "First paragraph\n\nSecond paragraph", fontSize: 10, maxChars: 200 });
  } });
  const original = host.created.filter(item => item.parent === view.widget.id);
  assert.equal(original.length, 2);
  const updateCount = host.updated.length;
  view.setText("First paragraph\n\nChanged paragraph");
  const after = host.created.filter(item => item.parent === view.widget.id);
  assert.deepEqual(after.map(item => item.id), original.map(item => item.id));
  assert.equal(host.updated.length, updateCount + 1, "unchanged first block should not be sent to native again");
  view.setFontSize(14);
  assert.ok(host.updated.some(item => item.id === original[0].id && item.props.fontSize === 14));
  assert.equal(view.source, "First paragraph\n\nChanged paragraph");
  view.dispose();
  assert.ok(host.removed.includes(view.widget.id));
  assert.ok(host.removed.includes(original[0].id));
  app.dispose();
});

test("changing a block kind rebuilds the suffix in native order", () => {
  const host = mockHost();
  let view!: ReturnType<typeof markdown>;
  const app = createApp({ setup(root) { view = markdown(root, { text: "# Heading\n\nBody" }); } });
  const rootId = view.widget.id;
  const previous = host.created.filter(item => item.parent === rootId);
  assert.deepEqual(previous.map(item => item.kind), ["richText", "richText"]);
  view.setText("Body\n\n---");
  const active = host.created.filter(item => item.parent === rootId && !host.removed.includes(item.id));
  assert.deepEqual(active.map(item => item.kind), ["richText", "box"]);
  assert.ok(active[0].id > previous.at(-1)!.id, "all children after the changed block are recreated after it");
  app.dispose();
});
