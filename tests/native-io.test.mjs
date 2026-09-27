import test from 'node:test';
import assert from 'node:assert/strict';
import { build } from 'esbuild';
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { mkdtemp, rm } from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

const root = path.resolve(import.meta.dirname, '..');
const host = path.join(root, 'native/build/cpzero-host');
async function run(source, frames = 100) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'cpzero-io-'));
  const bundle = path.join(dir, 'app.js');
  await build({ stdin: { contents: source, resolveDir: root, loader: 'ts' }, outfile: bundle, bundle: true, format: 'iife', target: 'es2020', alias: { '@cpzero/core': path.join(root, 'packages/core/src/index.ts') } });
  try {
    return await new Promise((resolve, reject) => {
      const child = spawn(host, [bundle, '--headless', '--frames', String(frames)], { env: { ...process.env, SDL_VIDEODRIVER: 'dummy', CPZERO_DATA_DIR: path.join(dir, 'data') } });
      let output = ''; const timer = setTimeout(() => { child.kill('SIGKILL'); reject(new Error(`native I/O test timed out\n${output}`)); }, Math.max(8000, frames * 20 + 2000));
      child.stdout.on('data', x => output += x); child.stderr.on('data', x => output += x);
      child.once('error', e => { clearTimeout(timer); reject(e); });
      child.once('close', code => { clearTimeout(timer); if (code !== 0) reject(new Error(`host exited ${code}\n${output}`)); else resolve(output); });
    });
  } finally { await rm(dir, { recursive: true, force: true }); }
}

test('process service streams stdout and stderr and reaps an interactive child without blocking UI ticks', async () => {
  const source = `import {createApp,processes,log,every} from '@cpzero/core'; createApp({setup(){ const p=processes.spawn({command:'/usr/bin/python3',args:['-u','-c',"import sys,time,os; print('ready'); print('warning',file=sys.stderr); os.write(1,bytes([226]));time.sleep(.02);os.write(1,bytes([130,172,10]));x=sys.stdin.readline();print('got:'+x.strip());time.sleep(.08)"]}); p.on('stdout',x=>log('OUT:'+x.trim()));p.on('stderr',x=>log('ERR:'+x.trim()));p.on('exit',x=>log('EXIT:'+x));p.write('hello\\n');p.closeStdin()}});`;
  const output = await run(source, 120);
  assert.match(output, /OUT:.*ready.*got:hello/s);
  assert.match(output, /ERR:warning/);
  assert.match(output, /€/);
  assert.match(output, /EXIT:0/);
});

test('HTTP service returns status, bounded headers, and body from local server asynchronously', async () => {
  let requests = 0;
  const server = createServer((req, res) => { requests++; res.writeHead(201, { 'content-type': 'text/plain', 'x-local': 'yes' }); res.end('local-response'); });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  try {
    const source = `import {createApp,http,log} from '@cpzero/core';createApp({setup(){log('START');http.request({url:'http://127.0.0.1:${address.port}/test'}).then(r=>log('HTTP:'+r.status+':'+r.headers['x-local']+':'+r.body),e=>log('HTTP_ERROR:'+e.message))}});`;
    const output = await run(source, 120);
    assert.equal(requests, 1); assert.match(output, /HTTP:201:yes:local-response/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});

test('process kill escalates when a child ignores SIGTERM', async () => {
  const source = `import {createApp,processes,log} from '@cpzero/core';createApp({setup(){const p=processes.spawn({command:'/usr/bin/python3',args:['-u','-c',"import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);print('ready',flush=True);time.sleep(5)"]});p.on('stdout',()=>p.kill());p.on('exit',code=>log('KILLED:'+code))}});`;
  const output = await run(source, 700);
  assert.match(output, /KILLED:137/);
});


test('HTTP POST copies its body and response limits reject oversized payloads', async () => {
  const server = createServer((req, res) => {
    const chunks = [];
    req.on('data', chunk => chunks.push(chunk));
    req.on('end', () => {
      if (req.url === '/large') { res.end(Buffer.alloc(2048, 'x')); return; }
      res.end(Buffer.concat(chunks));
    });
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  try {
    const source = `import {createApp,http,log} from '@cpzero/core';createApp({setup(){http.request({url:'http://127.0.0.1:${address.port}/echo',method:'POST',body:'copied-body'}).then(r=>log('POST:'+r.body));http.request({url:'http://127.0.0.1:${address.port}/large',maxBytes:100}).catch(e=>log('LIMIT:'+e.message))}});`;
    const output = await run(source, 140);
    assert.match(output, /POST:copied-body/);
    assert.match(output, /LIMIT:response body exceeds limit/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});

test('HTTP cancellation settles the promise without waiting for the server', async () => {
  const server = createServer((req, res) => setTimeout(() => res.end('late'), 500));
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  try {
    const source = `import {createApp,http,log} from '@cpzero/core';createApp({setup(){const task=http.start({url:'http://127.0.0.1:${address.port}/slow'});task.promise.catch(e=>log('CANCELLED:'+e.message));task.cancel()}});`;
    const output = await run(source, 80);
    assert.match(output, /CANCELLED:HTTP request cancelled/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});

test('a simultaneous HTTP burst overflows into a persistent terminal I/O error', async () => {
  const body = Buffer.alloc(450_000, 'z');
  let waiting = [];
  const server = createServer((req, res) => {
    waiting.push(res);
    if (waiting.length === 8) {
      for (const response of waiting) response.end(body);
      waiting = [];
    }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  try {
    const source = `import {createApp,http,log} from '@cpzero/core';createApp({setup(){for(let i=0;i<8;i++)http.request({url:'http://127.0.0.1:${address.port}/'+i,maxBytes:512*1024}).catch(e=>log('OVERFLOW:'+e.message))}});`;
    const output = await run(source, 180);
    assert.match(output, /OVERFLOW:event queue overflow/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});

test('completed process handles and pipe descriptors are reclaimed across repeated spawns', async () => {
  const source = `import {createApp,processes,log} from '@cpzero/core';createApp({setup(){let count=0;const next=()=>{const p=processes.spawn({command:'/usr/bin/python3',args:['-c',"pass"]});p.on('exit',()=>{if(++count===40)log('REAPED:40');else next()})};next()}});`;
  const output = await run(source, 1400);
  assert.match(output, /REAPED:40/);
});

test('a process group is cleaned up when its parent exits before a descendant', async () => {
  const source = `import {createApp,processes,log} from '@cpzero/core';createApp({setup(){const p=processes.spawn({command:'/usr/bin/python3',args:['-c',"import subprocess;subprocess.Popen(['/bin/sleep','5'])"]});p.on('exit',code=>log('GROUP_CLEANED:'+code))}});`;
  const output = await run(source, 900);
  assert.match(output, /GROUP_CLEANED:0/);
});
