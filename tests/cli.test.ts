import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { afterEach, test } from 'node:test';
import { atomicBundle, bundleApp, runCli } from '../packages/cli/src/index.mjs';

const temporary: string[] = [];
async function project() {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'cpzero-cli-'));
  temporary.push(dir);
  return dir;
}
afterEach(async () => { await Promise.all(temporary.splice(0).map((dir) => rm(dir, { recursive: true, force: true }))); });

test('init creates a minimal app with an SDK entry point and run scripts', async () => {
  const cwd = await project();
  const messages: string[] = [];
  assert.equal(await runCli(['init', 'hello-app'], { cwd, stdout: (s: string) => messages.push(s) }), 0);
  const appDir = path.join(cwd, 'hello-app');
  const manifest = JSON.parse(await readFile(path.join(appDir, 'package.json'), 'utf8'));
  assert.equal(manifest.scripts.dev, 'cpzero dev');
  assert.match(manifest.dependencies['@cpzero/core'], /^file:/);
  assert.match(manifest.devDependencies['@cpzero/cli'], /^file:/);
  assert.match(await readFile(path.join(appDir, 'src/main.ts'), 'utf8'), /createApp/);
  assert.match(await readFile(path.join(appDir, '.gitignore'), 'utf8'), /\.cpzero/);
  const packageDir = path.join(appDir, 'node_modules/@cpzero');
  const fs = await import('node:fs/promises');
  await fs.mkdir(packageDir, { recursive: true });
  await fs.symlink(path.resolve('packages/core'), path.join(packageDir, 'core'), 'dir');
  const bundleFile = path.join(cwd, 'scaffold.js');
  await bundleApp(appDir, 'src/main.ts', bundleFile);
  const source = await readFile(bundleFile, 'utf8');
  const vm = await import('node:vm');
  const calls: Array<[string, string, unknown]> = [];
  let nextId = 1;
  vm.runInNewContext(source, { __cp: {
    create: (kind: string, parent: number, props: unknown) => { calls.push(['create', kind, { parent, props }]); return nextId++; },
    update: () => {}, remove: () => {}, log: () => {}, invoke: () => '{}', stats: () => ({}),
  } });
  assert.deepEqual(calls.map((call) => call[1]), ['screen', 'column', 'label', 'button']);
  assert.deepEqual((calls[2]![2] as { props: { text: string } }).props.text, 'Hello from CPZeroJS');
  assert.match(messages[0]!, /npm install/);
});

test('build writes an IIFE bundle and a project manifest', async () => {
  const cwd = await project();
  const source = path.join(cwd, 'src/main.ts');
  const fs = await import('node:fs/promises');
  await fs.mkdir(path.dirname(source), { recursive: true });
  await fs.writeFile(source, 'globalThis.answer = 42;');
  assert.equal(await runCli(['build'], { cwd, stdout: () => {} }), 0);
  const bundle = await readFile(path.join(cwd, 'dist/app.js'), 'utf8');
  assert.match(bundle, /answer/);
  const manifest = JSON.parse(await readFile(path.join(cwd, 'dist/manifest.json'), 'utf8'));
  assert.equal(manifest.bundle, 'app.js');
  assert.equal(manifest.format, 'iife');
});

test('failed dev rebuild leaves the last good bundle intact', async () => {
  const cwd = await project();
  const source = path.join(cwd, 'src/main.ts');
  const fs = await import('node:fs/promises');
  await fs.mkdir(path.dirname(source), { recursive: true });
  await fs.writeFile(source, 'globalThis.version = 1;');
  const bundle = path.join(cwd, '.cpzero/app.js');
  await atomicBundle(cwd, 'src/main.ts', bundle);
  const previous = await readFile(bundle, 'utf8');
  await fs.writeFile(source, 'const broken = ;');
  await assert.rejects(atomicBundle(cwd, 'src/main.ts', bundle));
  assert.equal(await readFile(bundle, 'utf8'), previous);
});

test('dev reports a native simulator failure as a failed command', async () => {
  const cwd = await project();
  const fs = await import('node:fs/promises');
  await fs.mkdir(path.join(cwd, 'src'), { recursive: true });
  await fs.writeFile(path.join(cwd, 'src/main.ts'), 'globalThis.ready = true;');
  const host = path.join(cwd, 'host');
  await fs.writeFile(host, '#!/usr/bin/env node\nprocess.exit(7);\n');
  await fs.chmod(host, 0o755);
  const previousHost = process.env.CPZERO_HOST;
  process.env.CPZERO_HOST = host;
  const errors: string[] = [];
  try {
    assert.equal(await runCli(['dev'], { cwd, stderr: (message: string) => errors.push(message) }), 1);
    assert.match(errors.join('\n'), /Native simulator exited with code 7/);
  } finally {
    if (previousHost === undefined) delete process.env.CPZERO_HOST;
    else process.env.CPZERO_HOST = previousHost;
  }
});

test('dev watches imported modules and atomically gives the host the updated bundle', async () => {
  const cwd = await project();
  const fs = await import('node:fs/promises');
  const src = path.join(cwd, 'src');
  await fs.mkdir(src, { recursive: true });
  await fs.writeFile(path.join(src, 'main.ts'), "import { message } from './message'; globalThis.message = message;");
  await fs.writeFile(path.join(src, 'message.ts'), "export const message = 'INITIAL_VALUE';");
  const host = path.join(cwd, 'host');
  await fs.writeFile(host, `#!/usr/bin/env node
const fs = require('node:fs');
const bundle = process.argv[2];
let attempts = 0;
const timer = setInterval(() => {
  if (fs.readFileSync(bundle, 'utf8').includes('WATCH_UPDATED')) { clearInterval(timer); process.exit(0); }
  if (++attempts > 200) { clearInterval(timer); process.exit(11); }
}, 25);
`);
  await fs.chmod(host, 0o755);
  const previousHost = process.env.CPZERO_HOST;
  process.env.CPZERO_HOST = host;
  let started!: () => void;
  const startedPromise = new Promise<void>((resolve) => { started = resolve; });
  const messages: string[] = [];
  const running = runCli(['dev'], { cwd, stdout: (message: string) => {
    messages.push(message);
    if (message.includes('CPZeroJS dev:')) started();
  } });
  try {
    await startedPromise;
    await fs.writeFile(path.join(src, 'message.ts'), "export const message = 'WATCH_UPDATED';");
    assert.equal(await running, 0);
    assert.ok(messages.filter((message) => message === 'Rebuilt app.js').length >= 1);
    assert.match(await readFile(path.join(cwd, '.cpzero/app.js'), 'utf8'), /WATCH_UPDATED/);
  } finally {
    if (previousHost === undefined) delete process.env.CPZERO_HOST;
    else process.env.CPZERO_HOST = previousHost;
  }
});
