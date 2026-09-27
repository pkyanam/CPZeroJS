import test from 'node:test';
import assert from 'node:assert/strict';
import { build } from 'esbuild';
import { spawnSync, spawn } from 'node:child_process';
import { mkdir, mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

const root = path.resolve(import.meta.dirname, '..');
const host = path.join(root, 'native/build/cpzero-host');
const output = path.join(root, '.cpzero/test-output');
await mkdir(output, { recursive: true });

async function fixture(source) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'cpzero-native-'));
  const bundle = path.join(dir, 'app.js');
  await build({ stdin: { contents: source, resolveDir: root, loader: 'ts' }, outfile: bundle, bundle: true, format: 'iife', target: 'es2020', alias: { '@cpzero/core': path.join(root, 'packages/core/src/index.ts') } });
  return { dir, bundle, run(extra = []) { return spawnSync(host, [bundle, '--headless', '--frames', '20', ...extra], { encoding: 'utf8', timeout: 5000, env: { ...process.env, SDL_VIDEODRIVER: 'dummy', CPZERO_DATA_DIR: path.join(dir, 'data') } }); } };
}

test('native LVGL renders a real 320x170 frame and keyboard invokes TypeScript', async () => {
  const f = await fixture(`import { createApp, ui, log } from '@cpzero/core';
    createApp({setup(root) { root.update({bg:'#102030'}); ui.label(root,{text:'CPZeroJS'}); ui.button(root,{text:'Press',onPress(){log('NATIVE_PRESS_OK')}}); }});`);
  try {
    const shot = path.join(output, 'native.ppm');
    const result = f.run(['--keys', 'Enter', '--screenshot', shot]);
    assert.equal(result.status, 0, result.stderr || String(result.error));
    assert.match(result.stdout, /NATIVE_PRESS_OK/);
    const image = await readFile(shot);
    assert.equal(image.subarray(0, 15).toString(), 'P6\n320 170\n255\n');
    assert.equal(image.length, 15 + 320 * 170 * 3);
    assert.ok(new Set(image.subarray(15)).size > 8, 'frame must contain rendered text/widgets');
  } finally { await rm(f.dir, { recursive: true, force: true }); }
});

test('native storage round-trips JSON and the Promise job queue runs', async () => {
  const f = await fixture(`import {createApp,storage,log} from '@cpzero/core';
    createApp({setup(){void (async()=>{await storage.set('sample',{text:'hello',n:7});const v=await storage.get('sample');log('JSON:'+JSON.stringify(v));})()}});`);
  try { const r = f.run(); assert.equal(r.status, 0, r.stderr); assert.match(r.stdout, /JSON:\{"text":"hello","n":7\}/); }
  finally { await rm(f.dir, { recursive: true, force: true }); }
});

test('Tab and Enter follow actual focus order instead of arbitrary widget lookup', async () => {
  const f = await fixture(`import {createApp,ui,log} from '@cpzero/core';createApp({setup(root){ui.button(root,{text:'First',onPress(){log('FIRST')}});ui.button(root,{text:'Second',onPress(){log('SECOND')}})}});`);
  try { const r = f.run(['--keys', 'Tab,Enter']); assert.equal(r.status, 0, r.stderr); assert.match(r.stdout, /SECOND/); assert.doesNotMatch(r.stdout, /FIRST/); }
  finally { await rm(f.dir, { recursive: true, force: true }); }
});

test('a handled Promise rejection does not terminate a healthy app', async () => {
  const f = await fixture(`Promise.reject(new Error('expected')).catch(()=>__cp.log('RECOVERED'));`);
  try { const r = f.run(); assert.equal(r.status, 0, r.stderr); assert.match(r.stdout, /RECOVERED/); }
  finally { await rm(f.dir, { recursive: true, force: true }); }
});

test('programmatic input updates do not recursively re-enter JavaScript', async () => {
  const f = await fixture(`import {createApp,ui,log} from '@cpzero/core'; createApp({setup(root){const input=ui.input(root,{value:'before',onChange(){throw new Error('unexpected recursive change')}});input.update({value:'after'});log('UPDATE_OK')}});`);
  try { const r = f.run(); assert.equal(r.status, 0, r.stderr); assert.match(r.stdout, /UPDATE_OK/); }
  finally { await rm(f.dir, { recursive: true, force: true }); }
});

test('infinite startup and Promise work are interrupted instead of hanging', async () => {
  for (const source of ['while(true){}', 'Promise.resolve().then(()=>{while(true){}})']) {
    const f = await fixture(source);
    try { const r = f.run(); assert.notEqual(r.status, 0); assert.notEqual(r.error?.code, 'ETIMEDOUT', 'native deadline must terminate runaway JS'); }
    finally { await rm(f.dir, { recursive: true, force: true }); }
  }
});

test('native widget teardown returns live count to baseline across 1000 page changes', async () => {
  const f = await fixture(`import {createApp,ui,stats,log} from '@cpzero/core';createApp({setup(root){for(let i=0;i<1000;i++){const p=ui.column(root);ui.label(p,{text:'row'});p.dispose()}log('WIDGETS:'+stats().widgets)}});`);
  try { const r = f.run(); assert.equal(r.status, 0, r.stderr); assert.match(r.stdout, /WIDGETS:1\b/); }
  finally { await rm(f.dir, { recursive: true, force: true }); }
});

test('watch reload executes an updated bundle in a fresh app runtime', async () => {
  const f = await fixture(`__cp.log('FIRST_LOAD');`);
  try {
    const child = spawn(host, [f.bundle, '--headless', '--watch'], { env: { ...process.env, SDL_VIDEODRIVER: 'dummy', CPZERO_DATA_DIR: path.join(f.dir, 'data') }, stdio: ['ignore', 'pipe', 'pipe'] });
    let combined = '';
    child.stdout.on('data', data => { combined += data; });
    child.stderr.on('data', data => { combined += data; });
    const waitFor = async (text) => { const limit = Date.now()+4000; while(!combined.includes(text) && Date.now()<limit) await new Promise(r=>setTimeout(r,25)); assert.ok(combined.includes(text), combined || `missing ${text}`); };
    try { await waitFor('FIRST_LOAD'); await writeFile(f.bundle, `__cp.log('SECOND_LOAD');`); await waitFor('SECOND_LOAD'); }
    finally { child.kill('SIGTERM'); await new Promise(resolve=>child.once('exit',resolve)); }
  } finally { await rm(f.dir, { recursive: true, force: true }); }
});
