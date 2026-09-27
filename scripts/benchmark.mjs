import { build } from 'esbuild';
import { spawnSync } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

const root = path.resolve(import.meta.dirname, '..');
const directory = path.join(root, '.cpzero/benchmark');
await mkdir(directory, { recursive: true });
const outfile = path.join(directory, 'app.js');
await build({
  stdin: { contents: `import './examples/dashboard/src/main.ts'; import {stats,log} from '@cpzero/core'; log(JSON.stringify(stats()));`, resolveDir: root, loader: 'ts' },
  outfile, bundle: true, format: 'iife', target: 'es2020',
  alias: { '@cpzero/core': path.join(root, 'packages/core/src/index.ts') },
});
const started = performance.now();
const result = spawnSync(path.join(root, 'native/build/cpzero-host'), [outfile, '--headless', '--frames', '300'], {
  encoding: 'utf8', timeout: 10000,
  env: { ...process.env, SDL_VIDEODRIVER: 'dummy', CPZERO_DATA_DIR: path.join(directory, 'data') },
});
if (result.status !== 0) throw new Error(result.stderr || String(result.error));
const report = {
  environment: `${os.platform()} ${os.release()} ${os.arch()}`,
  workload: 'dashboard, headless SDL, 300 host iterations; not 300 displayed frames',
  wallMilliseconds: Math.round(performance.now() - started),
  ...JSON.parse(result.stdout.trim()),
  note: 'JS allocation and LVGL capacity are not total process memory. Desktop results do not predict Zero performance.',
};
await writeFile(path.join(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report, null, 2));
