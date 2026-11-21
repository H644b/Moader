#!/usr/bin/env node
'use strict';
// Build source-qualified SD inputs for the on-console FS adapter.
// This host tool never contacts a console or installs a hook by itself.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {buildAll, validateCapture} = require('../runtime/modloader/lib/native.cjs');
const {buildContext} = require('../runtime/modloader/lib/context.cjs');
const {plan, layout} = require('../runtime/modloader/lib/lazy-context.cjs');
const diagnostics = require('../runtime/modloader/lib/diagnostics.cjs');
const fsAbi = require('../runtime/modloader/lib/native/fs-abi.cjs');
const {artifacts, bridge: buildBridge} = require('../runtime/modloader/lib/native/artifacts.cjs');

const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const CAPTURE_SHA256 = '0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271';
const BASE = Object.freeze({fs: 0x4000000000n, context: 0x6000000000n,
  runtime: 0x7000000000n, filesystem: 0x6100001000n,
  logFile: 0x6100002000n, registry: 0x6100003000n});
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

function relocationsFor(baseline, varied, names) {
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
      ...Object.fromEntries(names.map((name, index) => [name, terms[index]]))});
  }
  for (let at = 0; at < baseline.length; ++at) {
    if (varied.some(bytes => bytes[at] !== baseline[at]))
      check(rows.some(row => at >= row.offset && at < row.offset + 8),
        'Relocation changed outside a qualified qword');
  }
  return rows;
}

function relocateFor(bytes, rows, from, to, names) {
  const output = Buffer.from(bytes);
  for (const row of rows) {
    const delta = names.reduce((sum, name) =>
      sum + BigInt(row[name]) * (to[name] - from[name]), 0n);
    output.writeBigUInt64LE(output.readBigUInt64LE(row.offset) + delta, row.offset);
  }
  return output;
}

const CODE_BASES = ['fs', 'context', 'runtime'];
const CONTEXT_BASES = ['fs', 'context', 'filesystem', 'logFile', 'registry'];
function relocations(baseline, varied) {
  return relocationsFor(baseline, varied, CODE_BASES);
}
function relocate(bytes, rows, from, to) {
  return relocateFor(bytes, rows, from, to, CODE_BASES);
}

function sourceAt(addresses) {
  return {filesystem: {object: addresses.filesystem}, registry: addresses.registry,
    logFile: {object: addresses.logFile, write: addresses.fs + 0x1050a0n,
      flush: addresses.fs + 0x1053d0n, setSize: addresses.fs + 0x105410n}};
}

function fsAbiBytes(captured) {
  return Buffer.concat(fsAbi.profile.ranges.map(range => {
    const bytes = captured.subarray(range.offset, range.offset + range.size);
    check(sha(bytes) === range.sha256, 'FS ABI range changed during payload preparation');
    return bytes;
  }));
}

function bridgeTemplates(caveOffset) {
  const names = [...Array.from({length: 7}, (_, count) => 'call' + count),
    'allocate', 'publish'];
  const result = {};
  for (const name of names) {
    const count = name.startsWith('call') ? Number(name.slice(4)) :
      name === 'allocate' ? 1 : 2;
    const args = Array(count).fill(0n);
    const options = {operation: BASE.fs + 0x694c0n,
      result: BASE.logFile, original_tail: BASE.fs + 0x1201c0n,
      clearAllocation: name === 'allocate'};
    const bytes = buildBridge(BASE.fs + BigInt(caveOffset), BASE.filesystem,
      args, options, name === 'publish');
    const symbols = artifacts().templates[name].symbols;
    const fields = Object.fromEntries(['owner', 'operation', 'result',
      ...args.map((_, index) => 'arg' + index)].map(key => {
      check(Number.isSafeInteger(symbols[key]?.value) &&
        symbols[key].value + 8 <= bytes.length,
      'Missing native bridge literal: ' + name + ':' + key);
      return [key, symbols[key].value];
    }));
    const checkBytes = Buffer.from(bytes);
    const different = {fs: BASE.fs + 3n * STEP,
      filesystem: BASE.filesystem + STEP, logFile: BASE.logFile - STEP};
    const replacements = {owner: different.filesystem,
      operation: different.fs + 0x694c0n, result: different.logFile};
    for (let index = 0; index < args.length; ++index)
      replacements['arg' + index] = BigInt(index + 1) * STEP;
    for (const [key, offset] of Object.entries(fields))
      checkBytes.writeBigUInt64LE(replacements[key], offset);
    const independentlyBuilt = buildBridge(different.fs + BigInt(caveOffset),
      different.filesystem, args.map((_, index) => replacements['arg' + index]),
      {operation: replacements.operation, result: replacements.result,
        original_tail: different.fs + 0x1201c0n,
        clearAllocation: name === 'allocate'}, name === 'publish');
    check(checkBytes.equals(independentlyBuilt),
      'Bridge template changes outside qualified literals: ' + name);
    result[name] = {hex: bytes.toString('hex'), bytes: bytes.length, fields};
  }
  return result;
}

function lua(value) {
  if (value === null) return 'nil';
  if (typeof value === 'string') return JSON.stringify(value);
  if (typeof value === 'number' || typeof value === 'boolean') return String(value);
  if (Array.isArray(value)) return '{' + value.map(lua).join(',') + '}';
  check(value && typeof value === 'object', 'Unsupported Lua profile value');
  return '{' + Object.entries(value).map(([key, item]) =>
    '[' + lua(key) + ']=' + lua(item)).join(',') + '}';
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
    for (const key of CODE_BASES)
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
    const context = buildContext(moader, sourceAt(BASE), BASE.fs, BASE.context, original);
    const variedContext = [];
    for (const key of CONTEXT_BASES) {
      const addresses = {...BASE, [key]: BASE[key] + STEP};
      const native = key === 'fs' || key === 'context'
        ? await build(addresses, 'context-' + key) : original;
      variedContext.push(buildContext(moader, sourceAt(addresses),
        addresses.fs, addresses.context, native));
    }
    const contextRelocations = relocationsFor(context, variedContext, CONTEXT_BASES);
    const contextProbe = {...BASE, fs: BASE.fs + 3n * STEP,
      context: BASE.context + 5n * STEP, filesystem: BASE.filesystem - STEP,
      logFile: BASE.logFile + 2n * STEP, registry: BASE.registry + 4n * STEP};
    const contextNative = await build(contextProbe, 'context-independent-check');
    check(relocateFor(context, contextRelocations, BASE, contextProbe, CONTEXT_BASES)
      .equals(buildContext(moader, sourceAt(contextProbe), contextProbe.fs,
        contextProbe.context, contextNative)),
      'Context relocation does not match an independent build');
    const abi = fsAbiBytes(captured);
    const manifest = {
      schema: 1, purpose: 'on-console FS installer candidate; device validation required',
      captureSha256: CAPTURE_SHA256, fsAbiProfile: qualified.profile,
      contextBytes: layout(moader).bytes,
      lazyOffset: layout(moader).lazyOffset,
      reportBytes: report.buffer.length,
      reportReadyOffset: report.readyOffset,
      baseline: Object.fromEntries(Object.entries(BASE).map(([key, value]) => [key, String(value)])),
      runtime: {file: 'runtime.bin', bytes: original.runtimeCode.length,
        sha256: sha(original.runtimeCode), relocations: runtimeRelocations},
      gateway: {file: 'gateway.bin', bytes: original.code.length,
        sha256: sha(original.code), relocations: gatewayRelocations},
      context: {file: 'context.bin', bytes: context.length,
        sha256: sha(context), relocations: contextRelocations},
      fsAbi: {file: 'fs-abi.bin', bytes: abi.length, sha256: sha(abi),
        ranges: fsAbi.profile.ranges.map(({offset, size, purpose}) => ({offset, size, purpose})),
        padding: fsAbi.profile.padding.used},
      hooks: original.hooks.map(({name, offset, originalHex, replacementHex}) =>
        ({name, offset, originalHex, replacementHex})),
      entries: original.offsets,
      bridge: {initial: bridgeTemplates(0x24d100),
        control: bridgeTemplates(0x24d800)},
      independentRelocationCheck: true, independentContextRelocationCheck: true,
    };
    check(!fs.existsSync(output), 'Output directory already exists');
    await fsp.mkdir(path.dirname(output), {recursive: true});
    const staging = await fsp.mkdtemp(path.join(path.dirname(output), '.moader-fs-payload-'));
    try {
      for (const [name, bytes] of [['runtime.bin', original.runtimeCode],
        ['gateway.bin', original.code], ['context.bin', context],
        ['fs-abi.bin', abi], ['runtime.log', report.buffer]])
        await fsp.writeFile(path.join(staging, name), bytes, {mode: 0o600});
      await fsp.writeFile(path.join(staging, 'manifest.json'),
        JSON.stringify(manifest, null, 2) + '\n', {mode: 0o600});
      await fsp.writeFile(path.join(staging, 'profile.lua'),
        '-- Generated from an exact qualified capture; SD candidate data only.\nreturn ' +
        lua(manifest) + '\n', {mode: 0o600});
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
    contextRelocations: manifest.context.relocations.length,
    independentRelocationCheck: manifest.independentRelocationCheck,
    independentContextRelocationCheck: manifest.independentContextRelocationCheck,
    bridgeTemplates: Object.values(manifest.bridge).reduce((n, group) =>
      n + Object.keys(group).length, 0),
    scope: manifest.purpose,
  }, null, 2))).catch(error => { console.error(error); process.exitCode = 1; });
}

module.exports = {prepare, relocations, relocate, relocationsFor, relocateFor,
  CAPTURE_SHA256};
