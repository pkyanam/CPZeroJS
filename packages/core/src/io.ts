/** Bounded async I/O. Native workers never execute JavaScript or draw widgets. */
type Cleanup = () => void;
type ProcessEvent = 'stdout' | 'stderr' | 'exit' | 'error';
type IoEvent = { type: string; id: number; data?: string; code?: number; error?: string; status?: number; body?: string; headers?: Record<string, string> };

function invoke<T>(service: string, method: string, ...args: unknown[]): T {
  if (!globalThis.__cp) throw new Error('Native I/O requires the CPZeroJS host.');
  return JSON.parse(globalThis.__cp.invoke(service, method, JSON.stringify(args))) as T;
}

export interface ProcessOptions { command: string; args?: string[]; cwd?: string }
const children = new Map<number, Process>();

/** A real child process; writes use stdin pipes, never a shell command string. */
export class Process {
  private listeners = new Map<ProcessEvent, Set<(value: string | number) => void>>();
  private closed = false;
  constructor(readonly id: number) {}
  get isClosed(): boolean { return this.closed; }
  write(text: string): void {
    if (this.closed) throw new Error('Process is closed.');
    invoke('process', 'write', this.id, text);
  }
  closeStdin(): void { if (!this.closed) invoke('process', 'closeStdin', this.id); }
  kill(): void { if (!this.closed) invoke('process', 'kill', this.id); }
  on(event: 'exit', callback: (code: number) => void): Cleanup;
  on(event: 'stdout' | 'stderr' | 'error', callback: (data: string) => void): Cleanup;
  on(event: ProcessEvent, callback: ((data: string) => void) | ((code: number) => void)): Cleanup {
    if (this.closed) throw new Error('Process is closed.');
    let set = this.listeners.get(event);
    if (!set) this.listeners.set(event, set = new Set());
    const fn = callback as (value: string | number) => void;
    set.add(fn);
    return () => { set?.delete(fn); };
  }
  /** @internal Events are dispatched only on the UI thread, from the shared pump. */
  dispatch(event: IoEvent): void {
    if (this.closed) return;
    const type = event.type.slice('process/'.length) as ProcessEvent;
    const value = type === 'exit' ? event.code ?? -1 : type === 'error' ? event.error ?? 'Process failed' : event.data ?? '';
    if (type === 'exit') { this.closed = true; children.delete(this.id); }
    try { for (const fn of [...(this.listeners.get(type) ?? [])]) {
      try { fn(value); } catch (error) {
        try { globalThis.__cp?.log(`I/O listener failed: ${String(error)}`); } catch { /* Continue draining the event batch. */ }
      }
    } }
    finally { if (type === 'exit') this.listeners.clear(); }
  }
  dispose(): void {
    if (this.closed) return;
    try { this.kill(); }
    finally { this.closed = true; children.delete(this.id); this.listeners.clear(); }
  }
}

export const processes = {
  spawn(options: ProcessOptions): Process {
    if (!options.command || options.command.includes('\0')) throw new Error('A valid process executable is required.');
    const process = new Process(invoke<number>('process', 'spawn', options));
    children.set(process.id, process);
    return process;
  },
};

export interface HttpOptions {
  url: string;
  method?: 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE' | 'HEAD' | 'OPTIONS';
  headers?: Record<string, string>;
  body?: string;
  timeoutMs?: number;
  maxBytes?: number;
}
export interface HttpResponse {
  status: number;
  ok: boolean;
  headers: Record<string, string>;
  body: string;
  json<T = unknown>(): T;
}
export interface HttpTask { promise: Promise<HttpResponse>; cancel(): void }
type HttpWaiter = { resolve(value: HttpResponse): void; reject(error: Error): void };
const requests = new Map<number, HttpWaiter>();

export const http = {
  start(options: HttpOptions): HttpTask {
    if (!/^https?:\/\//i.test(options.url)) throw new Error('HTTP requests require an http:// or https:// URL.');
    const id = invoke<number>('http', 'start', options);
    const promise = new Promise<HttpResponse>((resolve, reject) => { requests.set(id, { resolve, reject }); });
    return {
      promise,
      cancel() {
        const waiter = requests.get(id);
        if (!waiter) return;
        requests.delete(id);
        try { invoke('http', 'cancel', id); }
        finally { waiter.reject(new Error('HTTP request cancelled')); }
      },
    };
  },
  request(options: HttpOptions): Promise<HttpResponse> { return http.start(options).promise; },
};

/** @internal One bounded event batch per host tick keeps input/rendering responsive. */
export function pumpIO(): void {
  if (!children.size && !requests.size) return;
  const events = invoke<IoEvent[]>('io', 'poll');
  if (!Array.isArray(events)) throw new Error('Invalid native I/O event batch');
  for (const event of events) {
    if (event.type === 'io/error') {
      const error = event.error ?? 'Native I/O stopped';
      for (const child of [...children.values()]) {
        child.dispatch({ type: 'process/error', id: child.id, error });
        child.dispatch({ type: 'process/exit', id: child.id, code: -1 });
      }
      for (const pending of requests.values()) pending.reject(new Error(error));
      requests.clear();
      return;
    }
    if (event.type.startsWith('process/')) children.get(event.id)?.dispatch(event);
    else if (event.type === 'http/done' || event.type === 'http/error') {
      const pending = requests.get(event.id);
      if (!pending) continue;
      requests.delete(event.id);
      if (event.type === 'http/error') pending.reject(new Error(event.error ?? 'HTTP request failed'));
      else {
        const body = event.body ?? '';
        const status = event.status ?? 0;
        pending.resolve({ status, ok: status >= 200 && status < 300, headers: event.headers ?? {}, body, json<T>() { return JSON.parse(body) as T; } });
      }
    }
  }
}

/** @internal Explicit app disposal cancels native work and releases callbacks. */
export function disposeIO(): void {
  for (const child of [...children.values()]) { try { child.dispose(); } catch { /* Native shutdown will reap any surviving child. */ } }
  for (const [id, pending] of requests) {
    try { invoke('http', 'cancel', id); } catch { /* Host may already be shutting down. */ }
    pending.reject(new Error('Application disposed'));
  }
  requests.clear();
}
