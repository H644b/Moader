// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Exact-source private CJS graph for the Moader-held Seedsow producer.
// This loader materializes and checks code; the held provider separately admits
// the current FS and RO proofs before the installer can use them.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const Module = require('node:module');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const provider = require('./seedsow-provider.cjs');
const transforms = require('./seedsow-source-transforms.cjs');
const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const BUILTINS = new Set(['assert', 'assert/strict', 'buffer', 'crypto', 'events', 'fs', 'fs/promises',
  'net', 'os', 'path', 'perf_hooks', 'stream', 'timers', 'timers/promises', 'util', 'zlib']);
const TRANSFORMED = Object.freeze({
  'switch2/pipeline.cjs': 'af683824697d22c307421d735ba9d5d2aa0e3287a72e5cdfce0059d1b4deb1f4',
  'switch2/app/inspect-mounts.cjs': 'f9208ac6174e27d76e065af702a47163c9f0c72ed6a8708b7a90e3e1de0da012',
  'switch2/process/qualify.cjs': '32b97e4392cdd0b6a22ad831c0b8ae874d9227b69a819d92ac56c4af395606fe',
  'switch2/ro/launch.cjs': 'e45fd6a5de2af68ebe23ec32a440fce52ee539ddea4387f55a251df8706c9fa1',
  'switch2/ro/runtime.cjs': '90f1c99bf615b38e50c130e3686a058fdead3fe1099a612fbfe73016899b3bc9',
  'switch2/ro/nro.cjs': '5bf88f0c0bdb68d18f2f6e2e7d2804ab2ffc5835c37722514a1646e278d6b9c5',
  'switch2/ro/worker-control.cjs': '500e57525edb4f1c60b8fbf589691b9e76a8c8edc8fdd6ac9d9ebc13b1fe7b44',
  'switch2/ro/probe.cjs': '81964a994d03bf05fcb3d37e45184268d05c0811581679307f73fdafef9ee77e',
  'switch2/memory/probe-reserved-gap-seedsow.cjs': 'aa234e2db8b6151ba9cc4734d097386ba5934fe1d5385c51d5528fb9db09d97a',
  'switch2/memory/pinning-control.cjs': '3c6b9fa5ddee73686cf691dc1ecb030a7ce247453290a47817fbf5634d34bf5f',
  'switch2/memory/session.cjs': '40a6f3c994dc57e84e8601b17ed9eeba757d9e107b8c5ab624a51d5dad09def4',
  'switch2/memory/run-control.cjs': '1927eca18d2c2384a43c569e687355dd0c20cd4e689e38d3ed7357475a457d72',
  'switch2/retained/cache-shape.cjs': '7d7e5ed88cca52543a446dd1efa68dc3cdc1b5c9a149de189867086a6cf3865d',
  'switch2/retained/retained-memory.cjs': '73d2aaf1997a34e8821ad50262766489e7da1ee9001d0d339eddab1491accb88',
  'switch2/app/map.cjs': 'af10afe3740be93a5aab7d463d002a729d3fdd1236064b369f973d97c1f7bb44',
  'switch2/ro/install-retained.cjs': '1cd501af10826452064488a635d4b35ea91b7dfcc6e01f2c68cf09fe903aae0c',
  'switch2/ro/test-sm.cjs': '934374fe410eba7a3cebf2f1ba0846ebfa90e546892780dac2f5d1e36ad3c4e7',
  'switch2/ro/copy-loader-root.cjs': '3243a491e32a4cdaa8c2c2ea9b5e632a686ca88d2ad17488a566141b459a12c6',
  'switch2/ro/prepare-workers.cjs': 'ba40c68241251b8d05e3b38f3de656dc1d16339e26ef1e9a310918fd4857f27c',
  'switch2/ro/bind-sessions.cjs': '5ff7e8b1e705cdd882965ae9a0f549f2e43366be8a4c95660ae6bc719d8f4d12',
  'switch2/ro/prepare-event.cjs': 'b1e2ef5c22e506846096c6503b2fd2a40ba15953ffbcd392ff0b715eedfbaea2',
});
const TEST_FUNCTION = Object.freeze({
  source: 'switch2/process/test-function.S',
  sourceSha256: '9216c0201e021f4dfd625fe938bd863b02752c85d2a689d77a45acc0067f4eee',
  target: 'switch2/process/test-function.bin',
  sha256: '71544d5064344d40567311b57c6fd4d726c23346d02eec0bba4c210591401b42',
  bytes: 72,
  method: 'assemble-aarch64-text',
});
let boundRun = null;

function assembleTestFunction(sourceFile) {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'moader-seedsow-asm-'));
  try {
    const object = path.join(temporary, 'test-function.o');
    const binary = path.join(temporary, 'test-function.bin');
    for (const [command, args] of [
      [process.env.MOADER_AARCH64_CLANG || 'clang',
        ['-target', 'aarch64-none-elf', '-march=armv8.5-a', '-c', '-o', object, sourceFile]],
      [process.env.MOADER_AARCH64_OBJCOPY || 'aarch64-none-elf-objcopy',
        ['-O', 'binary', '--only-section=.text', object, binary]],
    ]) {
      const result = spawnSync(command, args, {encoding: 'utf8', timeout: 15000,
        maxBuffer: 256 * 1024});
      check(!result.error && result.status === 0,
        'Seedsow test-function assembly failed: ' + command + ': ' +
        (result.error?.message || result.stderr || result.stdout || result.status));
    }
    const bytes = fs.readFileSync(binary);
    check(bytes.length === TEST_FUNCTION.bytes && sha(bytes) === TEST_FUNCTION.sha256,
      'Seedsow assembled test-function differs from its pinned instruction fixture');
    return bytes;
  } finally { fs.rmSync(temporary, {recursive: true, force: true}); }
}

function materializeHeld({root, run}) {
  const profile = provider.resolveProfile(root);
  check(typeof run === 'string' && path.isAbsolute(run) && fs.realpathSync(run) === run &&
    fs.statSync(run).isDirectory() && fs.readdirSync(run).length === 0,
  'Seedsow held materialization requires a fresh empty canonical run');
  for (const [relative, expected] of Object.entries(profile.files)) {
    const file = path.join(profile.sourceDirectory, relative);
    check(fs.realpathSync(file) === file && fs.lstatSync(file).isFile() &&
      sha(fs.readFileSync(file)) === expected, 'Seedsow pinned source changed: ' + relative);
  }
  const files = {}, changed = {};
  for (const [relative, expected] of Object.entries(profile.runFiles)) {
    const input = path.join(profile.sourceDirectory, profile.engineRelativePath, relative);
    const original = fs.readFileSync(input);
    check(sha(original) === expected, 'Seedsow runtime source changed: ' + relative);
    let output = original;
    if (Object.hasOwn(TRANSFORMED, relative)) {
      const result = transforms.transform(relative, original.toString('utf8'));
      check(result.changed && result.sha256 === TRANSFORMED[relative],
        'Seedsow transformed source changed: ' + relative);
      output = Buffer.from(result.source);
      changed[relative] = result.sha256;
    }
    const dest = path.join(run, relative);
    fs.mkdirSync(path.dirname(dest), {recursive: true, mode: 0o700});
    fs.writeFileSync(dest, output, {flag: 'wx', mode: 0o400});
    files[relative] = sha(output);
  }
  check(profile.runFiles[TEST_FUNCTION.source] === TEST_FUNCTION.sourceSha256 &&
    !Object.hasOwn(profile.runFiles, TEST_FUNCTION.target),
  'Seedsow test-function derivation no longer matches the pinned source');
  const testFunction = assembleTestFunction(path.join(run, TEST_FUNCTION.source));
  const testFunctionFile = path.join(run, TEST_FUNCTION.target);
  fs.writeFileSync(testFunctionFile, testFunction, {flag: 'wx', mode: 0o400});
  const manifest = {schema: 1, provider: profile.id, revision: profile.revision,
    sourceTreeSha256: profile.sourceTreeSha256, files, transformed: changed,
    derived: {[TEST_FUNCTION.target]: {...TEST_FUNCTION}}};
  fs.writeFileSync(path.join(run, 'moader-source.json'), JSON.stringify(manifest, null, 2) + '\n',
    {flag: 'wx', mode: 0o400});
  return manifest;
}

function create({root, run, rpc, endpoint, heldProducer = false, stages, acquire}) {
  const profile = provider.resolveProfile(root);
  check(typeof run === 'string' && path.isAbsolute(run) && fs.realpathSync(run) === run &&
    fs.statSync(run).isDirectory(), 'Seedsow run path is not canonical');
  check(rpc && rpc.guard && rpc.a?.session === 'seedsow' && rpc.a.platform === 2,
    'Seedsow source graph requires the bound no-replay RPC adapter');
  check(endpoint && endpoint.guard === rpc.guard && typeof endpoint.request === 'function' &&
    typeof endpoint.read === 'function', 'Seedsow endpoint must share the RPC stop latch');
  if (heldProducer) check(stages && typeof stages.run === 'function' &&
    acquire && typeof acquire.start === 'function' && !acquire.retire && !acquire.abandon,
  'Seedsow held producer requires owned stage and acquisition adapters');
  check(!boundRun || boundRun === run, 'A different Seedsow run was already imported in this process');
  check(!process.env.NS2_RUN_DIR || path.resolve(process.env.NS2_RUN_DIR) === run,
    'An unrelated run is already bound to this process');
  boundRun = run;
  process.env.NS2_RUN_DIR = run;
  const engine = heldProducer ? run : path.join(profile.sourceDirectory, profile.engineRelativePath);
  const source = new Map();
  // Check every selected source byte before loading any of its executable code.
  for (const [relative, expected] of Object.entries(profile.files)) {
    const file = path.join(profile.sourceDirectory, relative);
    check(fs.realpathSync(file) === file && fs.lstatSync(file).isFile() &&
      sha(fs.readFileSync(file)) === expected, 'Seedsow source closure changed: ' + relative);
  }
  let manifest = null;
  if (heldProducer) {
    const file = path.join(run, 'moader-source.json');
    check(fs.realpathSync(file) === file && fs.lstatSync(file).isFile(),
      'Seedsow materialized source manifest is missing');
    manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
    check(manifest.schema === 1 && manifest.provider === profile.id &&
      manifest.revision === profile.revision && manifest.sourceTreeSha256 === profile.sourceTreeSha256 &&
      Object.keys(manifest.files || {}).length === Object.keys(profile.runFiles).length &&
      Object.keys(manifest.transformed || {}).length === Object.keys(TRANSFORMED).length,
    'Seedsow materialized source identity differs');
    check(Object.keys(manifest.derived || {}).length === 1 &&
      JSON.stringify(manifest.derived[TEST_FUNCTION.target]) === JSON.stringify(TEST_FUNCTION),
    'Seedsow derived test-function identity differs');
    const asset = path.join(run, TEST_FUNCTION.target);
    check(fs.realpathSync(asset) === asset && fs.lstatSync(asset).isFile() &&
      sha(fs.readFileSync(asset)) === TEST_FUNCTION.sha256,
    'Seedsow derived test-function changed before import');
  }
  const GENERATED_MOUNTS = 'switch2/app/mounts.json';
  function qualifyMounts() {
    check(heldProducer && !source.has(GENERATED_MOUNTS),
      'Seedsow mount inspection may be admitted once per fresh run');
    const filename = path.join(run, GENERATED_MOUNTS);
    check(fs.realpathSync(filename) === filename && fs.lstatSync(filename).isFile(),
      'Seedsow current mount inspection is not a regular run file');
    const bytes = fs.readFileSync(filename);
    check(bytes.length > 0 && bytes.length <= 128 * 1024,
      'Seedsow mount inspection exceeds its bound');
    const captured = JSON.parse(bytes);
    const pointer = value => Number.isSafeInteger(value) && value > 2 ** 32 && value < 2 ** 39;
    check(captured.session === 'seedsow' && captured.scope === 'save-only' &&
      captured.sdk === rpc.a.sdk && Array.isArray(captured.mounts) &&
      captured.mounts.length === 1 && captured.mounts[0]?.name === 'save' &&
      captured.mounts.every(row => typeof row.name === 'string' && row.name.length <= 16 &&
        pointer(row.accessor) && pointer(row.implementation) && pointer(row.service)) &&
      captured.mounts.filter(row => row.name === 'save').every(row =>
        pointer(row.serviceVtable) && /^[a-f0-9]{256}$/.test(row.serviceHex) &&
        /^[a-f0-9]{320}$/.test(row.serviceVtableHex)),
    'Seedsow current mount inspection is incomplete or has invalid pointers');
    const digest = sha(bytes);
    const proof = {schema: 1, session: 'seedsow', scope: 'save-only', sdk: rpc.a.sdk,
      file: GENERATED_MOUNTS, sha256: digest, mounts: captured.mounts.map(row => row.name),
      inspectorSha256: source.get('switch2/app/inspect-mounts.cjs')};
    fs.writeFileSync(path.join(run, 'moader-mounts.json'),
      JSON.stringify(proof, null, 2) + '\n', {flag: 'wx', mode: 0o600});
    source.set(GENERATED_MOUNTS, digest);
    return proof;
  }
  for (const [relative, expected] of Object.entries(profile.runFiles)) {
    const transformed = Object.hasOwn(TRANSFORMED, relative) ? TRANSFORMED[relative] : expected;
    check(!heldProducer || manifest.files[relative] === transformed &&
      (Object.hasOwn(TRANSFORMED, relative) ? manifest.transformed[relative] === transformed :
        !Object.hasOwn(manifest.transformed, relative)),
    'Seedsow materialized source manifest changed: ' + relative);
    source.set(relative, heldProducer ? transformed : expected);
  }
  const cache = new Map(), loaded = [];
  const endpointModule = Object.freeze({
    request: (socketPath, command, timeoutMs) => {
      check(socketPath === endpoint.socketPath, 'Seedsow retained socket path drifted');
      if (command?.command === 'retained-read')
        return endpoint.read(command.address, command.length, run, 'retained-read').then(row => row.result);
      return endpoint.request(command, timeoutMs);
    },
    read: (socketPath, address, length) => {
      check(socketPath === endpoint.socketPath, 'Seedsow retained socket path drifted');
      return endpoint.read(address, length, run);
    },
  });
  const overrides = new Map([['shared/rpc.cjs', rpc], ['switch2/tools/endpoint.cjs', endpointModule]]);
  if (heldProducer) {
    overrides.set('shared/stages.cjs', stages);
    overrides.set('switch2/acquire.cjs', acquire);
  }
  function load(relative) {
    check(typeof relative === 'string' && source.has(relative),
      'Module is outside the pinned Seedsow engine closure: ' + relative);
    if (overrides.has(relative)) return overrides.get(relative);
    if (cache.has(relative)) return cache.get(relative).exports;
    const filename = path.join(engine, relative);
    check(!require.cache[filename], 'Foreign cached module at Seedsow runtime path');
    check(fs.realpathSync(filename) === filename && fs.lstatSync(filename).isFile(),
      'Seedsow module path changed');
    const bytes = fs.readFileSync(filename);
    check(sha(bytes) === source.get(relative), 'Seedsow module changed before import: ' + relative);
    if (relative.endsWith('.json')) {
      const value = JSON.parse(bytes.toString('utf8'));
      cache.set(relative, {exports: value}); loaded.push(relative); return value;
    }
    check(relative.endsWith('.cjs'), 'Only pinned CJS or JSON can be imported');
    const module = new Module(filename);
    module.filename = filename;
    module.paths = Module._nodeModulePaths(path.dirname(filename));
    cache.set(relative, module); // Permit ordinary circular CJS imports.
    module.require = name => {
      if (name.startsWith('node:') || BUILTINS.has(name)) {
        const builtin = name.replace(/^node:/, '');
        check(BUILTINS.has(builtin), 'Unsafe builtin in Seedsow consumer graph: ' + name);
        return require(name);
      }
      check(name.startsWith('.') || path.isAbsolute(name),
        'External package is outside the Seedsow engine closure: ' + name);
      let file = path.isAbsolute(name) ? name : path.resolve(path.dirname(filename), name);
      if (!path.extname(file)) file += '.cjs';
      const child = path.relative(engine, file).split(path.sep).join('/');
      check(child && !child.startsWith('../') && !path.isAbsolute(child),
        'Seedsow import escaped the engine');
      return load(child);
    };
    try { module._compile(bytes.toString('utf8'), filename); module.loaded = true; loaded.push(relative); }
    catch (error) { cache.delete(relative); throw error; }
    return module.exports;
  }
  return Object.freeze({load, qualifyMounts, profile, engine, run, loaded: () => [...loaded],
    materialized: () => ({...(manifest?.transformed || {})}), sourceSha256: profile.sourceTreeSha256});
}
module.exports = {create, materializeHeld, TRANSFORMED, TEST_FUNCTION};
