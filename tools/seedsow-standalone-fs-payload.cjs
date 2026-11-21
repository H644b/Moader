#!/usr/bin/env node
'use strict';
// Offline preparation for a future on-console Moader FS installer. This tool
// cannot install a hook or turn the upstream standalone HEN run into Moader.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {buildAll, validateCapture} = require('../runtime/modloader/lib/native.cjs');
const {plan, layout} = require('../runtime/modloader/lib/lazy-context.cjs');
const diagnostics = require('../runtime/modloader/lib/diagnostics.cjs');

const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const CAPTURE_SHA256 = '0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271';
const BASE = Object.freeze({fs: 0x4000000000n, context: 0x6000000000n,
  runtime: 0x7000000000n});
const STEP = 0x100000000n;

function argumentsFrom(argv) {
  let capture, output;
  for (let i = 0; i < argv.length; ++i) {
    check(['--capture', '--output'].includes(argv[i]) && argv[i + 1],
      'Usage: node tools/seedsow-standalone-fs-payload.cjs --capture FS_CAPTURE --output NEW_DIRECTORY');
    if (argv[i] === '--capture') capture = path.resolve(argv[++i]);
    else output = path.resolve(argv[++i]);
  }
  check(capture && output, 'Both --capture and --output are required');
  return {capture, output};
}

function relocations(baseline, varied) {
  const names = ['fs', 'context', 'runtime'];
  check(varied.length === names.length && varied.every(bytes => bytes.length === baseline.length),
    'Relocation input lengths differ');
  const rows = [];
  for (let at = 0; at < baseline.length; at += 8) {
    const original = baseline.readBigUInt64LE(at);
    const terms = varied.map(bytes => {
      const difference = bytes.readBigUInt64LE(at) - original;
      check(difference === 0n || difference === STEP,
        'Nonlinear or non-qword relocation at 0x' + at.toString(16));
      return Number(difference / STEP);
    });
    if (terms.some(Boolean)) rows.push({offset: at,
      fs: terms[0], context: terms[1], runtime: terms[2]});
  }
  for (let at = 0; at < baseline.length; ++at) {
    if (varied.some(bytes => bytes[at] !== baseline[at]))
      check(rows.some(row => at >= row.offset && at < row.offset + 8),
        'Relocation changed outside a qualified qword');
  }
  return rows;
}

function relocate(bytes, rows, from, to) {
  const output = Buffer.from(bytes);
  for (const row of rows) {
    const delta = BigInt(row.fs) * (to.fs - from.fs) +
      BigInt(row.context) * (to.context - from.context) +
      BigInt(row.runtime) * (to.runtime - from.runtime);
    output.writeBigUInt64LE(output.readBigUInt64LE(row.offset) + delta, row.offset);
  }
  return output;
}

function sameHooks(first, second, base) {
  check(first.length === second.length, 'Hook count changed under relocation');
  for (let i = 0; i < first.length; ++i) {
    const a = first[i], b = second[i];
    check(a.offset === b.offset && a.originalHex === b.originalHex &&
      a.replacementHex === b.replacementHex && BigInt(b.target) === base + BigInt(a.offset),
    'A hook changed outside the FS base relocation: ' + a.name);
  }
}

async function prepare(capture, output) {
  check(fs.realpathSync(capture) === capture && fs.lstatSync(capture).isFile(),
    'Capture must be an existing canonical regular file');
  const captured = await fsp.readFile(capture);
  check(sha(captured) === CAPTURE_SHA256,
    'FS capture is not the exact previously verified Seedsow ABI snapshot');
  const qualified = validateCapture(captured);
  check(qualified.runtimeLoggingCompatible && qualified.validatedRanges === 36,
    'FS capture lacks the required Moader report ABI');
  const moader = plan({games: [], patches: []}, 'seedsow-standalone');
  moader.moader = true;
  const report = diagnostics.report({session: 'seedsow-standalone',
    sdPath: '/moader/runtime/runtime.log', collection: {games: [], patches: []}, moader,
    plan: moader});
  moader.diagnostics = {...report, text: report.buffer.toString('utf8'),
    size: report.buffer.length, buffer: undefined};
  const temp = await fsp.mkdtemp(path.join(os.tmpdir(), 'moader-fs-payload-'));
  const build = async (addresses, name) => buildAll(addresses.fs, addresses.context,
    moader, {capture, directory: path.join(temp, name), runtimeAddress: addresses.runtime});
  try {
    const original = await build(BASE, 'base');
    const changed = [];
    for (const key of ['fs', 'context', 'runtime'])
      changed.push(await build({...BASE, [key]: BASE[key] + STEP}, key));
    const runtimeRelocations = relocations(original.runtimeCode, changed.map(row => row.runtimeCode));
    const gatewayRelocations = relocations(original.code, changed.map(row => row.code));
    const probe = {fs: BASE.fs + 3n * STEP, context: BASE.context + 5n * STEP,
      runtime: BASE.runtime - 2n * STEP};
    const rebuilt = await build(probe, 'independent-check');
    check(relocate(original.runtimeCode, runtimeRelocations, BASE, probe)
      .equals(rebuilt.runtimeCode), 'Runtime relocation does not match an independent build');
    check(relocate(original.code, gatewayRelocations, BASE, probe)
      .equals(rebuilt.code), 'Gateway relocation does not match an independent build');
    sameHooks(original.hooks, rebuilt.hooks, probe.fs);
    check(original.hooks.length === 19 && original.runtimeCode.length === 106496 &&
      original.code.length <= 1024, 'Unexpected Moader FS payload topology');
    const manifest = {
      schema: 1, purpose: 'offline candidate only; no console installer',
      captureSha256: CAPTURE_SHA256, fsAbiProfile: qualified.profile,
      contextBytes: layout(moader).bytes, reportBytes: report.buffer.length,
      reportReadyOffset: report.readyOffset,
      baseline: Object.fromEntries(Object.entries(BASE).map(([key, value]) => [key, String(value)])),
      runtime: {file: 'runtime.bin', bytes: original.runtimeCode.length,
        sha256: sha(original.runtimeCode), relocations: runtimeRelocations},
      gateway: {file: 'gateway.bin', bytes: original.code.length,
        sha256: sha(original.code), relocations: gatewayRelocations},
      hooks: original.hooks.map(({name, offset, originalHex, replacementHex}) =>
        ({name, offset, originalHex, replacementHex})),
      entries: original.offsets,
      independentRelocationCheck: true,
    };
    check(!fs.existsSync(output), 'Output directory already exists');
    await fsp.mkdir(path.dirname(output), {recursive: true});
    const staging = await fsp.mkdtemp(path.join(path.dirname(output), '.moader-fs-payload-'));
    try {
      for (const [name, bytes] of [['runtime.bin', original.runtimeCode],
        ['gateway.bin', original.code], ['runtime.log', report.buffer]])
        await fsp.writeFile(path.join(staging, name), bytes, {mode: 0o600});
      await fsp.writeFile(path.join(staging, 'manifest.json'),
        JSON.stringify(manifest, null, 2) + '\n', {mode: 0o600});
      await fsp.rename(staging, output);
    } finally { await fsp.rm(staging, {recursive: true, force: true}); }
    return manifest;
  } finally { await fsp.rm(temp, {recursive: true, force: true}); }
}

if (require.main === module) {
  const {capture, output} = argumentsFrom(process.argv.slice(2));
  prepare(capture, output).then(manifest => console.log(JSON.stringify({
    output, captureSha256: manifest.captureSha256, contextBytes: manifest.contextBytes,
    runtimeBytes: manifest.runtime.bytes, gatewayBytes: manifest.gateway.bytes,
    hooks: manifest.hooks.length, runtimeRelocations: manifest.runtime.relocations.length,
    gatewayRelocations: manifest.gateway.relocations.length,
    independentRelocationCheck: manifest.independentRelocationCheck,
    scope: manifest.purpose,
  }, null, 2))).catch(error => { console.error(error); process.exitCode = 1; });
}

module.exports = {prepare, relocations, relocate, CAPTURE_SHA256};
