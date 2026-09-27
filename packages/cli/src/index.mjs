#!/usr/bin/env node
import { build, context } from "esbuild";
import { spawn } from "node:child_process";
import { randomUUID } from "node:crypto";
import { realpathSync } from "node:fs";
import { access, mkdir, rename, rm, writeFile, readdir, symlink, readlink } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
const cliDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(cliDir, "../../..");
const help = `CPZeroJS \u2014 build and run tiny native apps

Usage:
  cpzero init <directory>       Create a ready-to-run app
  cpzero dev [entry]             Build and run with native hot reload
  cpzero build [entry]           Build dist/app.js and manifest.json
  cpzero doctor                  Check local build and simulator setup
  cpzero --help                  Show this help

Options for dev:
  --scale <n>                    Simulator scale (default: 3)
  --headless                     Run without a window
  --frames <n>                    Exit after n frames
  --screenshot <path>             Save a native PPM screenshot
`;
const out = (options, text) => (options.stdout ?? console.log)(text);
const err = (options, text) => (options.stderr ?? console.error)(text);
function parseFlags(args) {
  const positional = [];
  const flags = {};
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (!arg.startsWith("--")) {
      positional.push(arg);
      continue;
    }
    const key = arg.slice(2);
    if (key === "headless") flags[key] = true;
    else if (["scale", "frames", "screenshot"].includes(key)) {
      const value = args[++i];
      if (!value || value.startsWith("--")) throw new Error(`--${key} requires a value`);
      flags[key] = value;
    } else throw new Error(`Unknown option: ${arg}`);
  }
  return { positional, flags };
}
function entryPath(cwd, entry) {
  return path.resolve(cwd, entry ?? "src/main.ts");
}
function coreResolve(root, cwd) {
  if (path.resolve(cwd) !== root) return void 0;
  return { "@cpzero/core": path.join(root, "packages/core/src/index.ts"), "@cpzero/codex": path.join(root, "packages/codex/src/index.ts") };
}
async function bundleApp(cwd, entry, outfile, root = repoRoot, write = true) {
  const opts = {
    absWorkingDir: cwd,
    entryPoints: [entryPath(cwd, entry)],
    outfile,
    bundle: true,
    platform: "browser",
    format: "iife",
    target: "es2020",
    sourcemap: false,
    logLevel: "silent",
    write,
    alias: coreResolve(root, cwd)
  };
  return build(opts);
}
async function atomicBundle(cwd, entry, outfile, root = repoRoot) {
  const dir = path.dirname(outfile);
  await mkdir(dir, { recursive: true });
  const temporary = `${outfile}.${process.pid}.${randomUUID()}.tmp.js`;
  try {
    const result = await bundleApp(cwd, entry, temporary, root, false);
    const output = result.outputFiles?.[0];
    if (!output) throw new Error("esbuild produced no app bundle");
    await writeFile(temporary, output.contents);
    await rename(temporary, outfile);
  } catch (e) {
    await rm(temporary, { force: true });
    throw e;
  }
}
function nativeHost(root) {
  return process.env.CPZERO_HOST ?? path.join(root, "native/build/cpzero-host");
}
async function runHost(host, bundle, args, cwd) {
  await access(host);
  // A real app bundle gives the Mac simulator a Dock identity and reliable focus.
  if (process.platform === "darwin" && !args.includes("--headless") && !process.env.CPZERO_HOST) {
    const contents = path.join(cwd, ".cpzero/CPZeroJS.app/Contents");
    const binary = path.join(contents, "MacOS/cpzero-host");
    await mkdir(path.dirname(binary), { recursive: true });
    await writeFile(path.join(contents, "Info.plist"), `<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>dev.cpzero.simulator</string>
<key>CFBundleName</key><string>CPZeroJS</string>
<key>CFBundleExecutable</key><string>cpzero-host</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>0.2.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
`);
    if (await readlink(binary).catch(() => "") !== host) {
      await rm(binary, { force: true });
      await symlink(host, binary);
    }
    host = binary;
  }
  const child = spawn(host, [bundle, ...args], { cwd, stdio: "inherit", env: { ...process.env, CPZERO_DATA_DIR: process.env.CPZERO_DATA_DIR ?? path.join(cwd, ".cpzero/data") } });
  child.on("error", (e) => console.error(`cpzero: simulator failed: ${e.message}`));
  return child;
}
async function buildCommand(cwd, entry, root, options) {
  const dist = path.join(cwd, "dist");
  const outfile = path.join(dist, "app.js");
  await mkdir(dist, { recursive: true });
  await bundleApp(cwd, entry, outfile, root);
  const manifest = {
    name: path.basename(cwd),
    entry,
    bundle: "app.js",
    format: "iife",
    target: "es2020",
    builtAt: (/* @__PURE__ */ new Date()).toISOString()
  };
  await writeFile(path.join(dist, "manifest.json"), `${JSON.stringify(manifest, null, 2)}
`);
  out(options, `Built ${path.relative(cwd, outfile)} and dist/manifest.json`);
}
async function devCommand(cwd, entry, flags, root, options) {
  const outfile = path.join(cwd, ".cpzero/app.js");
  await mkdir(path.dirname(outfile), { recursive: true });
  await atomicBundle(cwd, entry, outfile, root);
  const hostArgs = ["--watch"];
  if (flags.scale) hostArgs.push("--scale", String(flags.scale));
  if (flags.headless) hostArgs.push("--headless");
  if (flags.frames) hostArgs.push("--frames", String(flags.frames));
  if (flags.screenshot) hostArgs.push("--screenshot", String(flags.screenshot));
  let closed = false;
  let stoppingForSignal = false;
  const buildContext = await context({
    absWorkingDir: cwd,
    entryPoints: [entryPath(cwd, entry)],
    outfile,
    bundle: true,
    platform: "browser",
    format: "iife",
    target: "es2020",
    write: false,
    logLevel: "silent",
    alias: coreResolve(root, cwd),
    plugins: [{
      name: "cpzero-atomic-output",
      setup(buildContext2) {
        buildContext2.onEnd(async (result) => {
          if (result.errors.length) {
            err(options, `Build failed; keeping the last working app bundle.
${result.errors.map((e) => e.text).join("\n")}`);
            return;
          }
          const output = result.outputFiles?.[0];
          if (output) {
            const temporary = `${outfile}.${process.pid}.${randomUUID()}.tmp.js`;
            try {
              await writeFile(temporary, output.contents);
              await rename(temporary, outfile);
              out(options, "Rebuilt app.js");
            } catch (e) {
              await rm(temporary, { force: true });
              err(options, `Could not replace app bundle: ${e instanceof Error ? e.message : String(e)}`);
            }
          }
        });
      }
    }]
  });
  const host = await runHost(nativeHost(root), outfile, hostArgs, cwd);
  // Attach immediately: a failing executable can exit before esbuild starts watching.
  const hostDone = new Promise(resolve => {
    host.once("error", error => resolve({ code: 1, signal: null, error }));
    host.once("exit", (code, signal) => resolve({ code, signal }));
  });
  const stop = async () => {
    if (closed) return;
    closed = true;
    process.removeListener("SIGINT", stopSignal);
    process.removeListener("SIGTERM", stopSignal);
    await buildContext.dispose();
    if (host.exitCode === null && host.signalCode === null) host.kill("SIGTERM");
  };
  const stopSignal = () => {
    stoppingForSignal = true;
    void stop();
  };
  process.once("SIGINT", stopSignal);
  process.once("SIGTERM", stopSignal);
  let exit;
  try {
    await buildContext.watch();
    out(options, `CPZeroJS dev: ${entry} → native simulator (watching for changes)`);
    exit = await hostDone;
  } finally {
    await stop();
  }
  if (exit.error) throw exit.error;
  if (exit.code !== 0 && !stoppingForSignal) throw new Error(`Native simulator exited ${exit.signal ? `on ${exit.signal}` : `with code ${exit.code}`}`);
}
async function runCli(argv, options = {}) {
  const cwd = path.resolve(options.cwd ?? process.cwd());
  const [command, ...rawArgs] = argv;
  if (!command || command === "--help" || command === "-h" || command === "help") {
    out(options, help);
    return 0;
  }
  if (command === "init") {
    const target = rawArgs[0];
    if (!target) {
      err(options, "Usage: cpzero init <directory>");
      return 2;
    }
    const dir = path.resolve(cwd, target);
    const existing = await readdir(dir).catch(() => []);
    if (existing.length) {
      err(options, `Refusing to initialize non-empty directory: ${dir}`);
      return 1;
    }
    const coreDependency = `file:${path.join(repoRoot, "packages/core")}`;
    const cliDependency = `file:${path.join(repoRoot, "packages/cli")}`;
    const files = {
      "package.json": JSON.stringify({ name: path.basename(dir), private: true, type: "module", scripts: { dev: "cpzero dev", build: "cpzero build" }, dependencies: { "@cpzero/core": coreDependency }, devDependencies: { "@cpzero/cli": cliDependency } }, null, 2) + "\n",
      "src/main.ts": `import { createApp, ui } from '@cpzero/core';

createApp({
  title: ${JSON.stringify(path.basename(dir))},
  setup(root) {
    const column = ui.column(root, { gap: 8, padding: 12 });
    ui.label(column, { text: 'Hello from CPZeroJS', fontSize: 18 });
    ui.button(column, { text: 'It works!' });
  },
});
`,
      ".gitignore": "node_modules/\ndist/\n.cpzero/\n"
    };
    await mkdir(path.join(dir, "src"), { recursive: true });
    for (const [name, contents] of Object.entries(files)) await writeFile(path.join(dir, name), contents);
    out(options, `Created CPZeroJS app in ${path.relative(cwd, dir) || "."}
Next: cd ${path.relative(cwd, dir) || "."} && npm install && npm run dev`);
    return 0;
  }
  if (command === "doctor") {
    const checks = [
      ["Node.js", async () => process.versions.node],
      ["esbuild", async () => import("esbuild")],
      ["CPZero native simulator", async () => access(nativeHost(repoRoot))]
    ];
    let failed = false;
    for (const [name, check] of checks) {
      try {
        const value = await check();
        out(options, `\u2713 ${name}${typeof value === "string" ? ` ${value}` : ""}`);
      } catch {
        failed = true;
        err(options, `\u2717 ${name} unavailable`);
      }
    }
    if (process.env.CPZERO_HOST) out(options, `Using CPZERO_HOST=${process.env.CPZERO_HOST}`);
    return failed ? 1 : 0;
  }
  try {
    const { positional, flags } = parseFlags(rawArgs);
    const entry = positional[0] ?? "src/main.ts";
    if (command === "build") {
      await buildCommand(cwd, entry, repoRoot, options);
      return 0;
    }
    if (command === "dev") {
      await devCommand(cwd, entry, flags, repoRoot, options);
      return 0;
    }
    err(options, `Unknown command: ${command}

${help}`);
    return 2;
  } catch (e) {
    err(options, e instanceof Error ? e.message : String(e));
    return 1;
  }
}
if (process.argv[1] && realpathSync(process.argv[1]) === realpathSync(fileURLToPath(import.meta.url))) {
  runCli(process.argv.slice(2)).then((code) => {
    process.exitCode = code;
  }).catch((e) => {
    console.error(e);
    process.exitCode = 1;
  });
}
export {
  atomicBundle,
  bundleApp,
  runCli
};
