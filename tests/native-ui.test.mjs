import test from 'node:test';
import assert from 'node:assert/strict';
import { build } from 'esbuild';
import { spawnSync } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

const root = path.resolve(import.meta.dirname, '..');
const host = path.join(root, 'native/build/cpzero-host');
async function run(source, args = []) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'cpzero-ui-'));
  try {
    const bundle = path.join(dir, 'app.js');
    await build({ stdin: { contents: source, resolveDir: root, loader: 'ts' }, outfile: bundle, bundle: true, format: 'iife', target: 'es2020', alias: { '@cpzero/core': path.join(root, 'packages/core/src/index.ts') } });
    return spawnSync(host, [bundle, '--headless', '--frames', '20', ...args], { encoding: 'utf8', timeout: 5000, env: { ...process.env, SDL_VIDEODRIVER: 'dummy', CPZERO_DATA_DIR: path.join(dir, 'data') } });
  } finally { await rm(dir, { recursive: true, force: true }); }
}

test('single-line input accepts SDL text and submits through LVGL keyboard events', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core'; createApp({setup(root){const input=ui.input(root,{value:'',onChange:v=>log('VALUE:'+v),onSubmit:v=>log('SUBMIT:'+v)});input.focus();}});`, ['--text', 'café', '--keys', 'Enter']);
  assert.equal(r.status, 0, r.stderr || String(r.error));
  assert.match(r.stdout, /VALUE:café/);
  assert.match(r.stdout, /SUBMIT:café/);
  assert.equal([...r.stdout.matchAll(/VALUE:café/g)].length, 1, 'text must reach the textarea once');
});

test('native widget commands return input values and scroll offsets', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core'; createApp({setup(root){const input=ui.input(root,{value:'ready'}); const pane=ui.column(root,{width:120,height:40,scroll:'vertical'}); for(let i=0;i<8;i++)ui.label(pane,{text:'row '+i,fontSize:10}); pane.scrollToEnd(); log('GET:'+input.getValue()+':SCROLL:'+pane.getScrollY());}});`);
  assert.equal(r.status, 0, r.stderr);
  assert.match(r.stdout, /GET:ready:SCROLL:[1-9]\d*/);
});

test('global key handler consumes injected keyboard input before LVGL focus', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core'; createApp({setup(root){const app=root.app;ui.button(root,{text:'A',onPress(){log('PRESSED')}}); app.onKey(e=>{log('KEY:'+e.key);return e.key==='Enter'});}});`, ['--keys', 'Enter']);
  assert.equal(r.status, 0, r.stderr);
  assert.doesNotMatch(r.stdout, /PRESSED/);
  assert.match(r.stdout,/KEY:Enter/);
});

test('SDL Tab and Enter stay inside a trapped focus scope and activate its button', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core';createApp({setup(root){const bg=ui.button(root,{text:'Background',onPress(){log('BACKGROUND')}});const modal=ui.column(root,{width:200,height:80});const hidden=ui.column(modal,{hidden:true});ui.button(hidden,{text:'Hidden'});const first=ui.button(modal,{text:'Allow',onPress(){log('ALLOWED')}});ui.button(modal,{text:'Deny',onPress(){log('DENIED');modal.command('releaseFocus');log('RESTORED:'+bg.isFocused())}});modal.command('trapFocus',first.id);log('FOCUSED:'+first.isFocused());}});`, ['--keys', 'Tab,Enter']);
  assert.equal(r.status, 0, r.stderr);
  assert.match(r.stdout,/FOCUSED:true/);
  assert.match(r.stdout,/DENIED/);
  assert.doesNotMatch(r.stdout,/BACKGROUND/);
  assert.equal([...r.stdout.matchAll(/DENIED/g)].length,1);
  assert.doesNotMatch(r.stdout,/ALLOWED/);
  assert.match(r.stdout,/RESTORED:true/);
});

test('same-batch SDL keydown/up releases Enter and advances Tab once', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core';createApp({setup(root){const a=ui.button(root,{text:'A',onPress(){log('A_PRESS')},onFocus(){log('A_FOCUS')}});ui.button(root,{text:'B',onPress(){log('B_PRESS')},onFocus(){log('B_FOCUS')}});}});`, ['--keys-batch','Enter,Tab,Enter']);
  assert.equal(r.status,0,r.stderr);
  assert.equal([...r.stdout.matchAll(/A_PRESS/g)].length,1);
  assert.equal([...r.stdout.matchAll(/B_PRESS/g)].length,1);
  assert.equal([...r.stdout.matchAll(/B_FOCUS/g)].length,1);
});

test('one SDL Tab press is released instead of repeating indefinitely', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core';createApp({setup(root){ui.button(root,{text:'A'});ui.button(root,{text:'B',onFocus(){log('B_FOCUS')}})}});`, ['--keys-batch','Tab','--frames','800']);
  assert.equal(r.status,0,r.stderr);
  assert.equal([...r.stdout.matchAll(/B_FOCUS/g)].length,1,`focus must advance once then remain stable: ${r.stdout}`);
});

test('logical mouse click uses the LVGL pointer input path', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core';createApp({setup(root){const layout=ui.column(root,{width:320,height:170,padding:0,gap:0});ui.box(layout,{width:120,height:80});const row=ui.row(layout,{width:320,height:30,gap:0});ui.box(row,{width:100,height:30});ui.button(row,{text:'Click',width:80,height:30,onPress(){log('MOUSE_PRESS')}})}});`, ['--scale','4','--click', '110,90']);
  assert.equal(r.status, 0, r.stderr);
  assert.equal([...r.stdout.matchAll(/MOUSE_PRESS/g)].length,2,'rapid SDL down/up pairs must both be delivered');
});

test('a click focuses an input before queued text is delivered in the same frame', async () => {
  const r = await run(`import {createApp,ui,log} from '@cpzero/core';createApp({setup(root){const row=ui.row(root,{width:320,height:30,gap:0});ui.button(row,{text:'Other',width:60,height:30});ui.input(row,{width:180,height:30,value:'',onChange:v=>log('VALUE:'+v)});}});`, ['--scale','4','--click','70,10','--text','hello']);
  assert.equal(r.status,0,r.stderr);
  assert.match(r.stdout,/VALUE:hello/);
});

test('rich text spans render with mixed styles and reject unbounded input', async () => {
  const source = `import {createApp,log} from '@cpzero/core';createApp({setup(root){root.app.create(root,'richText',{width:320,fontSize:12,spans:[{text:'It’s “clear”… ',color:'#102030'},{text:'→ done',fontSize:10,color:'#1769e0'}]});log('RICH_TEXT_OK')}});`;
  const rendered = await run(source);
  assert.equal(rendered.status, 0, rendered.stderr);
  assert.match(rendered.stdout,/RICH_TEXT_OK/);
  const overflow = await run(`import {createApp} from '@cpzero/core';createApp({setup(root){root.app.create(root,'richText',{spans:Array.from({length:257},()=>({text:'x'}))})}});`);
  assert.notEqual(overflow.status,0);
  assert.match(overflow.stderr,/at most 256 spans/);
});
