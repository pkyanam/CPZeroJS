/**
 * Small Codex app-server adapter. Protocol subset verified against the generated
 * schema shipped with Codex CLI 0.157.1 (2026-09).
 */
import { after as coreAfter, log as coreLog, processes } from "@cpzero/core";
import type { ProcessOptions } from "@cpzero/core";

export type ProcessExit = { code: number };
export interface CodexTransport {
  write(text: string): void;
  closeStdin(): void;
  kill(): void;
  on(event: "exit", callback: (value: number) => void): () => void;
  on(event: "stdout" | "stderr" | "error", callback: (value: string) => void): () => void;
  dispose(): void;
}

export type CodexEvent =
  | { type: "connected"; userAgent?: string }
  | { type: "account"; account: unknown }
  | { type: "delta"; threadId?: string; turnId?: string; itemId?: string; text: string }
  | CodexItemEvent
  | { type: "activity"; method: string; params: unknown }
  | { type: "approval" | "user-input" | "server-request"; id: string | number; method: string; params: unknown }
  | { type: "error"; error: Error }
  | { type: "closed"; code?: number };

export type CodexItemPhase = "started" | "completed" | "output" | "progress";
export type CodexItemEvent = {
  type: "item";
  phase: CodexItemPhase;
  itemId?: string;
  itemType: string;
  threadId?: string;
  turnId?: string;
  summary?: string;
  detail?: string;
  output?: string;
  status?: string;
  success?: boolean;
};
export type CodexApprovalChoice = "accept" | "decline";

export interface CodexClientOptions {
  command?: string;
  cwd?: string;
  maxLineBytes?: number;
  maxTranscriptChars?: number;
  maxItemFieldChars?: number;
  requestTimeoutMs?: number;
  /** Core `after()`-compatible timeout hook. Defaults to the active core app timer. */
  schedule?: (ms: number, callback: () => void) => () => void;
  /** Inject for deterministic tests or an alternate process host. */
  transport?: CodexTransport;
  spawn?: (options: ProcessOptions) => CodexTransport;
}

type JsonRpc = { id?: string | number; method?: string; params?: any; result?: any; error?: { code?: number; message?: string; data?: unknown } };
type Listener = (event: CodexEvent) => void;
type Pending = { resolve: (value: any) => void; reject: (error: Error) => void; cancelTimer: () => void };

const errorOf = (value: unknown): Error => value instanceof Error ? value : new Error(String(value));
const asRecord = (v: unknown): Record<string, any> => v && typeof v === "object" ? v as Record<string, any> : {};

/** App-server client with explicit approval routing and bounded stream storage. */
export class CodexClient {
  private readonly transport: CodexTransport;
  private readonly maxLine: number;
  private readonly maxTranscript: number;
  private readonly maxItemField: number;
  private readonly timeout: number;
  private readonly schedule: (ms: number, callback: () => void) => () => void;
  private readonly cwd?: string;
  private listeners = new Set<Listener>();
  private unsubs: (() => void)[] = [];
  private pending = new Map<string, Pending>();
  private serverRequests = new Map<string, string>();
  private lineParts: string[] = [];
  private lineBytes = 0;
  private transcriptValue = "";
  private nextId = 1;
  private closed = false;
  private ready = false;
  private threadId?: string;
  private activeTurn?: string;
  private lastCompletedTurn?: string;
  private turnStart?: Promise<string>;

  constructor(options: CodexClientOptions = {}) {
    this.maxLine = options.maxLineBytes ?? 1024 * 1024;
    this.maxTranscript = options.maxTranscriptChars ?? 64 * 1024;
    this.maxItemField = options.maxItemFieldChars ?? 4096;
    if (!Number.isInteger(this.maxItemField) || this.maxItemField < 1) throw new RangeError("maxItemFieldChars must be a positive integer.");
    this.timeout = options.requestTimeoutMs ?? 15_000;
    this.schedule = options.schedule ?? coreAfter;
    this.cwd = options.cwd;
    if (options.transport) this.transport = options.transport;
    else {
      if (!options.spawn) throw new Error("CodexClient requires transport or spawn().");
      this.transport = options.spawn({ command: options.command ?? "codex", args: ["app-server", "--stdio"], cwd: this.cwd });
    }
    this.unsubs.push(
      this.transport.on("stdout", chunk => this.receive(String(chunk))),
      this.transport.on("stderr", chunk => { if (String(chunk)) this.emit({ type: "activity", method: "stderr", params: String(chunk).slice(0, 4096) }); }),
      this.transport.on("error", error => this.fail(errorOf(error))),
      this.transport.on("exit", code => this.fail(new Error(`Codex app-server exited (${code}).`), typeof code === "number" ? code : undefined)),
    );
  }

  on(listener: Listener): () => void { this.listeners.add(listener); return () => this.listeners.delete(listener); }
  get transcript(): string { return this.transcriptValue; }
  get currentThreadId(): string | undefined { return this.threadId; }
  get isConnected(): boolean { return this.ready && !this.closed; }

  async connect(): Promise<unknown> {
    if (this.ready) return this.request("account/read", {});
    const init = await this.request("initialize", { clientInfo: { name: "cpzero", title: "CPZero", version: "0.2.0" }, capabilities: null });
    this.notify("initialized", {});
    this.ready = true;
    this.emit({ type: "connected", userAgent: asRecord(init).userAgent });
    const account = await this.request("account/read", {});
    this.emit({ type: "account", account });
    return account;
  }

  async newThread(options: { cwd?: string; model?: string } = {}): Promise<string> {
    const result = await this.request("thread/start", {
      cwd: options.cwd ?? this.cwd,
      model: options.model,
      approvalPolicy: "on-request",
      sandbox: "read-only",
    });
    const thread = asRecord(result).thread;
    if (!thread?.id) throw new Error("Codex app-server returned no thread ID.");
    this.threadId = String(thread.id);
    this.transcriptValue = "";
    this.activeTurn = undefined;
    this.lastCompletedTurn = undefined;
    return this.threadId;
  }

  async resume(threadId: string): Promise<void> {
    if (!threadId) throw new Error("threadId is required.");
    await this.request("thread/resume", { threadId, cwd: this.cwd });
    this.threadId = threadId;
  }

  async send(text: string): Promise<string> {
    if (!this.threadId) throw new Error("Start or resume a thread before sending a message.");
    if (!text.trim()) throw new Error("Message cannot be empty.");
    if (this.activeTurn || this.turnStart) throw new Error("A turn is already starting or running.");
    const starting = this.request("turn/start", { threadId: this.threadId, input: [{ type: "text", text, text_elements: [] }] })
      .then(result => {
        const turnId = asRecord(result).turn?.id;
        if (turnId && this.lastCompletedTurn !== turnId) this.activeTurn = String(turnId);
        return String(turnId ?? "");
      });
    this.turnStart = starting;
    try { return await starting; }
    finally { if (this.turnStart === starting) this.turnStart = undefined; }
  }

  async cancel(): Promise<void> {
    const threadId = this.threadId;
    if (!threadId) return;
    let turnId = this.activeTurn;
    if (this.turnStart) turnId = (await this.turnStart) || turnId;
    if (!turnId || this.lastCompletedTurn === turnId || this.threadId !== threadId) return;
    await this.request("turn/interrupt", { threadId, turnId });
  }

  /** Reply to an app-server request only after the UI receives and resolves it. */
  respond(id: string | number, result: unknown): void {
    this.assertOpen();
    this.serverRequests.delete(String(id));
    this.write({ id, result });
  }

  deny(id: string | number, message = "Denied by user."): void {
    this.assertOpen();
    this.serverRequests.delete(String(id));
    this.write({ id, error: { code: -32000, message } });
  }

  /** Resolve a supported approval request with the exact decision shape for its method. */
  resolveApproval(id: string | number, choice: CodexApprovalChoice, rejection = "Declined by user."): void {
    this.assertOpen();
    const method = this.serverRequests.get(String(id));
    if (!method) throw new Error("Approval request is unknown or has already been resolved.");
    const v2Command = method === "item/commandExecution/requestApproval";
    const v2File = method === "item/fileChange/requestApproval";
    const legacy = method === "execCommandApproval" || method === "applyPatchApproval";
    if (!v2Command && !v2File && !legacy) throw new Error(`Unsupported approval method: ${method}`);
    const decision = v2Command || v2File
      ? choice === "accept" ? "accept" : "decline"
      : choice === "accept" ? "approved" : { denied: { rejection: bounded(rejection, this.maxItemField) } };
    this.respond(id, { decision });
  }

  dispose(): void {
    if (this.closed) return;
    this.closed = true;
    this.unsubs.splice(0).forEach(unsub => unsub());
    this.rejectPending(new Error("Codex client disposed."));
    this.lineParts = [];
    this.lineBytes = 0;
    this.serverRequests.clear();
    try { this.transport.closeStdin(); } catch { /* best effort */ }
    try { this.transport.kill(); } catch { /* best effort */ }
    this.transport.dispose();
    this.emit({ type: "closed" });
    this.listeners.clear();
  }

  private async request(method: string, params: unknown): Promise<any> {
    this.assertOpen();
    const id = this.nextId++;
    const key = String(id);
    return new Promise((resolve, reject) => {
      const cancelTimer = this.schedule(this.timeout, () => {
        this.pending.delete(key);
        reject(new Error(`Codex request timed out: ${method}`));
      });
      this.pending.set(key, { resolve, reject, cancelTimer });
      try { this.write({ id, method, params }); } catch (error) {
        cancelTimer(); this.pending.delete(key); reject(errorOf(error));
      }
    });
  }

  private notify(method: string, params: unknown): void { this.assertOpen(); this.write({ method, params }); }
  private write(message: unknown): void { this.transport.write(`${JSON.stringify(message)}\n`); }
  private assertOpen(): void { if (this.closed) throw new Error("Codex client is closed."); }

  private receive(chunk: string): void {
    if (this.closed) return;
    let cursor = 0;
    while (cursor < chunk.length) {
      const newline = chunk.indexOf("\n", cursor);
      const end = newline < 0 ? chunk.length : newline;
      const part = chunk.slice(cursor, end);
      this.lineBytes += utf8Length(part);
      if (this.lineBytes > this.maxLine) {
        this.lineParts = [];
        this.lineBytes = 0;
        this.fail(new Error("Codex protocol line exceeded configured limit.")); return;
      }
      if (part) this.lineParts.push(part);
      if (newline < 0) break;
      const line = this.lineParts.join("").trim();
      this.lineParts = [];
      this.lineBytes = 0;
      cursor = newline + 1;
      if (!line) continue;
      let msg: JsonRpc;
      try { msg = JSON.parse(line) as JsonRpc; }
      catch { this.fail(new Error("Codex app-server sent invalid JSON.")); return; }
      this.dispatch(msg);
      if (this.closed) return;
    }
  }

  private dispatch(msg: JsonRpc): void {
    if (msg.id !== undefined && !msg.method) {
      const pending = this.pending.get(String(msg.id));
      if (!pending) return;
      this.pending.delete(String(msg.id)); pending.cancelTimer();
      if (msg.error) pending.reject(new Error(msg.error.message ?? "Codex request failed."));
      else pending.resolve(msg.result);
      return;
    }
    if (!msg.method) return;
    const p = asRecord(msg.params);
    if (msg.id !== undefined) {
      const type = msg.method.includes("requestApproval") || msg.method === "execCommandApproval" || msg.method === "applyPatchApproval" ? "approval"
        : msg.method.includes("requestUserInput") ? "user-input" : "server-request";
      if (this.serverRequests.size >= 128) this.serverRequests.delete(this.serverRequests.keys().next().value!);
      this.serverRequests.set(String(msg.id), msg.method);
      this.emit({ type, id: msg.id, method: msg.method, params: p } as CodexEvent);
      return;
    }
    if (msg.method === "item/agentMessage/delta") {
      const delta = String(p.delta ?? "");
      if (delta) {
        this.transcriptValue = (this.transcriptValue + delta).slice(-this.maxTranscript);
        const clipped = bounded(delta, this.maxItemField);
        this.emit({ type: "delta", threadId: p.threadId, turnId: p.turnId, itemId: p.itemId, text: clipped });
        this.emit(itemEvent("output", "agentMessage", p, { output: clipped }));
      }
      this.emitActivity(msg.method, p);
    } else if (msg.method === "item/started" || msg.method === "item/completed") {
      const phase = msg.method === "item/started" ? "started" : "completed";
      this.emit(normalizeThreadItem(phase, p, this.maxItemField));
      this.emitActivity(msg.method, p);
    } else if (msg.method === "item/commandExecution/outputDelta" || msg.method === "item/fileChange/outputDelta") {
      const type = msg.method.includes("commandExecution") ? "commandExecution" : "fileChange";
      this.emit(itemEvent("output", type, p, { output: bounded(p.delta, this.maxItemField) }));
      this.emitActivity(msg.method, p);
    } else if (msg.method === "item/plan/delta") {
      this.emit(itemEvent("output", "plan", p, { output: bounded(p.delta, this.maxItemField) }));
      this.emitActivity(msg.method, p);
    } else if (msg.method === "mcpToolCall/progress" || msg.method === "item/mcpToolCall/progress") {
      this.emit(itemEvent("progress", "mcpToolCall", p, { summary: "Tool progress", detail: bounded(p.message, this.maxItemField) }));
      this.emitActivity(msg.method, p);
    } else if (msg.method === "turn/plan/updated") {
      const steps = Array.isArray(p.plan) ? p.plan : [];
      const detail = [p.explanation, ...steps.map((step: any) => `${step.status ?? ""} ${step.step ?? step.title ?? ""}`)].filter(Boolean).join("\n");
      this.emit(itemEvent("progress", "plan", p, { summary: `${steps.length} plan steps`, detail: bounded(detail, this.maxItemField) }));
      this.emitActivity(msg.method, p);
    } else if (msg.method === "turn/diff/updated") {
      this.emit(itemEvent("progress", "diff", p, { summary: "Workspace diff updated", output: bounded(p.diff, this.maxItemField) }));
      this.emitActivity(msg.method, p);
    } else if (msg.method === "serverRequest/resolved") {
      this.serverRequests.delete(String(p.requestId));
      this.emitActivity(msg.method, p);
    } else if (msg.method === "error") {
      const detail = p.error ?? p.message ?? p;
      this.emit({ type: "error", error: new Error(bounded(detail, this.maxItemField)) });
      this.emitActivity(msg.method, p);
    } else if (msg.method === "turn/started") {
      this.activeTurn = p.turn?.id ?? p.turnId ?? this.activeTurn;
      this.emitActivity(msg.method, p);
    } else if (msg.method === "turn/completed") {
      this.lastCompletedTurn = p.turn?.id ?? p.turnId ?? this.activeTurn;
      this.activeTurn = undefined;
      this.emitActivity(msg.method, p);
    } else this.emitActivity(msg.method, p);
  }

  private emitActivity(method: string, params: unknown): void { this.emit({ type: "activity", method, params }); }

  private fail(error: Error, code?: number): void {
    if (this.closed) return;
    this.closed = true;
    this.rejectPending(error);
    this.lineParts = [];
    this.lineBytes = 0;
    this.serverRequests.clear();
    this.emit({ type: "error", error });
    this.emit({ type: "closed", code });
    this.unsubs.splice(0).forEach(unsub => unsub());
    try { this.transport.closeStdin(); } catch { /* best effort */ }
    if (code === undefined) { try { this.transport.kill(); } catch { /* best effort */ } }
    try { this.transport.dispose(); } catch { /* best effort */ }
    this.listeners.clear();
  }
  private rejectPending(error: Error): void {
    for (const pending of this.pending.values()) { pending.cancelTimer(); pending.reject(error); }
    this.pending.clear();
  }
  private emit(event: CodexEvent): void {
    for (const listener of [...this.listeners]) {
      try { listener(event); }
      catch (error) { try { coreLog(`Codex event listener failed: ${bounded(errorOf(error).message, 512)}`); } catch { /* Logging is best-effort during teardown. */ } }
    }
  }
}

/** Process bridge matching the v0.2 core processes API. */
export function connectCodex(options: Omit<CodexClientOptions, "transport"> = {}): CodexClient {
  const spawn = options.spawn ?? (opts => processes.spawn(opts));
  return new CodexClient({ ...options, spawn });
}

/** Byte count without browser or Node globals, including astral Unicode. */
function utf8Length(value: string): number {
  let bytes = 0;
  for (const character of value) {
    const code = character.codePointAt(0)!;
    bytes += code <= 0x7f ? 1 : code <= 0x7ff ? 2 : code <= 0xffff ? 3 : 4;
  }
  return bytes;
}

function bounded(value: unknown, limit = 4096): string {
  let text: string;
  if (typeof value === "string") text = value;
  else {
    try { text = value === undefined || value === null ? "" : JSON.stringify(value); }
    catch { text = String(value); }
  }
  return text.length <= limit ? text : `${text.slice(0, Math.max(0, limit - 1))}…`;
}

function itemEvent(
  phase: CodexItemPhase,
  itemType: string,
  params: Record<string, any>,
  fields: Partial<Pick<CodexItemEvent, "summary" | "detail" | "output" | "status" | "success">> = {}
): CodexItemEvent {
  return {
    type: "item", phase, itemType,
    itemId: params.itemId ?? params.item?.id,
    threadId: params.threadId,
    turnId: params.turnId,
    ...fields
  };
}

function normalizeThreadItem(phase: "started" | "completed", params: Record<string, any>, limit: number): CodexItemEvent {
  const item = asRecord(params.item);
  const type = String(item.type ?? "unknown");
  let summary = type;
  let detail = "";
  let output = "";
  let status = typeof item.status === "string" ? bounded(item.status, limit) : undefined;
  let success: boolean | undefined;
  switch (type) {
    case "agentMessage":
      summary = "Assistant";
      output = phase === "completed" ? bounded(item.text, limit) : "";
      break;
    case "commandExecution":
      summary = bounded(item.command || "Command", limit);
      detail = bounded(item.cwd || "", limit);
      output = phase === "completed" ? bounded(item.aggregatedOutput, limit) : "";
      status = status ?? (phase === "completed" ? "completed" : "running");
      if (item.exitCode !== null && item.exitCode !== undefined) success = item.exitCode === 0;
      break;
    case "fileChange": {
      const changes = Array.isArray(item.changes) ? item.changes : [];
      summary = `${changes.length} file change${changes.length === 1 ? "" : "s"}`;
      status = status ?? bounded(item.status ?? "", limit);
      detail = bounded(changes.map((change: any) => `${change.path ?? ""} (${change.kind ?? "updated"})`).join("\n"), limit);
      output = phase === "completed" ? bounded(changes.map((change: any) => `${change.path ?? ""}\n${change.diff ?? ""}`).join("\n"), limit) : "";
      success = phase === "completed" && item.status !== "failed";
      break;
    }
    case "mcpToolCall": {
      summary = bounded(`${item.server ?? "MCP"}: ${item.tool ?? "tool"}`, limit);
      detail = bounded(item.arguments, limit);
      status = status ?? "";
      output = phase === "completed" ? bounded(item.error?.message ?? item.result?.structuredContent ?? item.result?.content ?? "", limit) : "";
      success = phase === "completed" ? item.status === "completed" : undefined;
      break;
    }
    case "webSearch":
      summary = bounded(item.query || "Web search", limit);
      status = status ?? (phase === "completed" ? "completed" : "running");
      output = phase === "completed" ? bounded(item.results, limit) : "";
      break;
    case "imageGeneration":
      summary = "Image generation";
      detail = bounded(item.revisedPrompt ?? item.failure?.type ?? "", limit);
      status = status ?? "";
      output = phase === "completed" ? bounded(item.savedPath ?? item.failure?.type ?? "", limit) : "";
      success = phase === "completed" ? item.status === "succeeded" || item.status === "completed" : undefined;
      break;
    case "plan":
      summary = "Plan";
      output = phase === "completed" ? bounded(item.text, limit) : "";
      break;
    default:
      summary = type;
      output = phase === "completed" ? bounded(item.text ?? "", limit) : "";
  }
  return itemEvent(phase, type, params, { summary, detail, output, status, success });
}
