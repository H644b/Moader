// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Typed, consume-only view of the pinned Seedsow n2h_rpc.lua binary protocol.
// The caller supplies an exclusively owned socket through seedsow-transport.
// This module neither connects nor imports the upstream bridge, whose fallback
// and reconnect paths can replay a completed native operation.

const MAX_TRANSFER = 16 * 1024 * 1024;
const CHUNK = 0x80000; // Below the console's 0x90000-byte PAY buffer.
const MAX_WORKERS = 16, MAX_CALLS = 64;
const check = (ok, message) => { if (!ok) throw Error(message); };

function integer(value, name, maximum = Number.MAX_SAFE_INTEGER) {
  check(Number.isSafeInteger(value) && value >= 0 && value <= maximum, 'Invalid Seedsow ' + name);
  return value;
}
function scalar(value, name) {
  check(typeof value === 'bigint' || typeof value === 'number' && Number.isSafeInteger(value) ||
    typeof value === 'string' && /^(?:0x[0-9a-f]+|[0-9]+)$/i.test(value), 'Invalid Seedsow ' + name);
  const number = BigInt(value);
  check(number >= 0n && number <= 0xffffffffffffffffn, 'Seedsow ' + name + ' exceeds u64');
  return number;
}
function address(value, name) {
  const number = scalar(value, name);
  check(number > 0n && number <= BigInt(Number.MAX_SAFE_INTEGER), 'Invalid Seedsow ' + name);
  return number;
}
function safeNumber(value, name) {
  check(value >= 0n && value <= BigInt(Number.MAX_SAFE_INTEGER), 'Invalid Seedsow ' + name);
  return Number(value);
}
function exactObject(value, keys, name) {
  check(value && typeof value === 'object' && !Array.isArray(value) &&
    Object.keys(value).every(key => keys.includes(key)), 'Invalid Seedsow ' + name);
}
function decodeStatus(reply) {
  check(reply.aux === 0n && reply.payload.length > 0 && reply.payload.length <= 256,
    'Invalid Seedsow status reply');
  const parts = reply.payload.toString('ascii').split('\0');
  check(parts.length === 5 && parts[0] === 'S' && parts[4] === 'seedsow' &&
    parts.slice(1, 4).every(part => /^(?:0|[1-9][0-9]*)$/.test(part)),
  'Invalid Seedsow status identity');
  return {sdk: integer(Number(parts[1]), 'SDK base'), main: integer(Number(parts[2]), 'main base'),
    scratch: integer(Number(parts[3]), 'scratch base'), session: parts[4]};
}
function frameWorkers(workers) {
  check(Array.isArray(workers) && workers.length > 0 && workers.length <= MAX_WORKERS,
    'Invalid Seedsow worker count');
  const parts = [Buffer.alloc(8)];
  parts[0].writeUInt32LE(workers.length, 0);
  const specs = [];
  for (const worker of workers) {
    exactObject(worker, ['base', 'limit', 'slot', 'snapshot', 'calls', 'priority', 'affinity'], 'worker');
    check(Array.isArray(worker.calls) && worker.calls.length > 0 && worker.calls.length <= MAX_CALLS,
      'Invalid Seedsow worker calls');
    const base = address(worker.base, 'worker base'), limit = scalar(worker.limit ?? 0x40000, 'worker limit');
    const slot = address(worker.slot, 'worker slot'), snapshot = scalar(worker.snapshot ?? 0, 'worker snapshot');
    check(base % 0x1000n === 0n && limit >= 0x1000n && limit <= BigInt(MAX_TRANSFER) &&
      slot % 8n === 0n && slot + 16n <= base, 'Invalid Seedsow worker frame');
    check(typeof (worker.affinity ?? false) === 'boolean', 'Invalid Seedsow worker affinity');
    const priority = integer(worker.priority ?? 0, 'worker priority', 0xffffffff);
    const spec = Buffer.alloc(48);
    for (const [at, value] of [[0, base], [8, limit], [16, slot], [24, snapshot],
      [32, BigInt(worker.calls.length)]]) spec.writeBigUInt64LE(value, at);
    spec.writeUInt32LE(priority, 40);
    spec.writeUInt32LE(worker.affinity ? 1 : 0, 44);
    parts.push(spec);
    let nativeCalls = 0;
    for (const call of worker.calls) {
      // The pinned probe annotates IPC calls for its local report. The
      // upstream worker encoder ignores these two fields on the wire.
      exactObject(call, ['function', 'args', 'name', 'kind'], 'worker call');
      check(call.name === undefined || typeof call.name === 'string' &&
        call.name.length > 0 && call.name.length <= 128,
      'Invalid Seedsow worker call name');
      check(call.kind === undefined || call.kind === 'ipc',
        'Invalid Seedsow worker call kind');
      const args = call.args ?? [];
      check(Array.isArray(args) && args.length <= 4, 'Seedsow supports at most four worker arguments');
      const rec = Buffer.alloc(48);
      rec.writeBigUInt64LE(address(call.function, 'worker function'), 0);
      rec.writeUInt8(args.length, 8);
      for (let i = 0; i < 4; ++i) {
        const arg = args[i] ?? 0;
        if (arg && typeof arg === 'object') {
          exactObject(arg, ['u32'], 'dynamic worker argument');
          check(Object.hasOwn(arg, 'u32'), 'Missing dynamic worker argument value');
          rec.writeUInt8(1, 9 + i);
          rec.writeBigUInt64LE(scalar(arg.u32, 'dynamic worker argument'), 16 + i * 8);
          ++nativeCalls;
        } else rec.writeBigUInt64LE(scalar(arg, 'worker argument'), 16 + i * 8);
      }
      ++nativeCalls;
      parts.push(rec);
    }
    specs.push({base, limit, nativeCalls, logicalCalls: worker.calls.length});
  }
  const payload = Buffer.concat(parts);
  check(payload.length <= CHUNK, 'Seedsow worker request exceeds console buffer');
  return {payload, specs};
}
function decodeStarted(reply, specs, owned) {
  const data = reply.payload;
  check(reply.aux === BigInt(specs.length) && data.length >= 8 &&
    data.readUInt32LE(0) === specs.length && data.readUInt32LE(4) === 0,
  'Invalid Seedsow worker start count');
  let at = 8;
  const rows = [], newIndices = new Set();
  for (const spec of specs) {
    check(data.length - at >= 56, 'Truncated Seedsow worker start metadata');
    const thread = data.readBigUInt64LE(at), bytes = data.readBigUInt64LE(at + 8);
    const resultOffset = data.readBigUInt64LE(at + 16), cancelCell = data.readBigUInt64LE(at + 24);
    const startCell = data.readBigUInt64LE(at + 32), index = data.readBigUInt64LE(at + 40);
    const nativeCalls = data.readUInt32LE(at + 48), logicalCalls = data.readUInt32LE(at + 52);
    at += 56;
    check(thread > 0n && thread < 0x1000000000000n && bytes > 0n && bytes <= spec.limit &&
      resultOffset < bytes && cancelCell >= spec.base && cancelCell < spec.base + spec.limit &&
      nativeCalls === spec.nativeCalls && logicalCalls === spec.logicalCalls &&
      index > 0n && index <= 0xffffffffn && !newIndices.has(index.toString()) && !owned.has(index.toString()),
    'Invalid Seedsow worker ownership metadata');
    check(data.length - at >= nativeCalls * 8 + logicalCalls * 4,
      'Truncated Seedsow worker result metadata');
    const resultOffsets = [];
    for (let i = 0; i < nativeCalls; ++i) {
      const value = data.readBigUInt64LE(at + i * 8);
      check(value < bytes && (i === 0 || value > BigInt(resultOffsets[i - 1])),
        'Invalid Seedsow worker result offset');
      resultOffsets.push(Number(value));
    }
    at += nativeCalls * 8;
    const logicalResultIndices = [];
    for (let i = 0; i < logicalCalls; ++i) {
      const value = data.readUInt32LE(at + i * 4);
      check(value < nativeCalls && (i === 0 || value > logicalResultIndices[i - 1]),
        'Invalid Seedsow logical result index');
      logicalResultIndices.push(value);
    }
    at += logicalCalls * 4;
    check(resultOffset === BigInt(resultOffsets.at(-1)), 'Seedsow final worker result differs');
    const row = {thread: Number(thread), bytes: Number(bytes), resultOffset: Number(resultOffset),
      cancelCell: safeNumber(cancelCell, 'worker cancellation cell'), startCell: startCell.toString(),
      index: Number(index), nativeCalls, logicalCalls, resultOffsets, logicalResultIndices};
    rows.push(row);
    newIndices.add(index.toString());
  }
  check(at === data.length, 'Trailing Seedsow worker start metadata');
  for (const row of rows) owned.set(String(row.index), row);
  return rows;
}
function decodeJoined(reply, selected, owned) {
  const data = reply.payload;
  check(reply.aux === BigInt(selected.length) && data.length >= 8 &&
    data.readUInt32LE(0) === selected.length && data.readUInt32LE(4) === 0,
  'Invalid Seedsow worker join count');
  let at = 8;
  const out = [];
  for (const worker of selected) {
    check(data.length - at >= 16, 'Truncated Seedsow worker join metadata');
    const exitValue = data.readBigUInt64LE(at), n = data.readBigUInt64LE(at + 8);
    at += 16;
    check(n === BigInt(worker.logicalCalls) && data.length - at >= Number(n) * 8,
      'Invalid Seedsow worker join result count');
    const results = [];
    for (let i = 0; i < Number(n); ++i) results.push(data.readBigUInt64LE(at + i * 8).toString());
    at += Number(n) * 8;
    out.push({results, exitValue: exitValue.toString()});
  }
  check(at === data.length, 'Trailing Seedsow worker join metadata');
  for (const worker of selected) owned.delete(String(worker.index));
  return out;
}

function create(transport, expected) {
  check(transport && typeof transport.request === 'function' && transport.guard &&
    typeof transport.guard.fail === 'function', 'Seedsow typed RPC requires an owned transport');
  exactObject(expected, ['platform', 'sdk', 'main', 'scratch', 'session'], 'connection binding');
  check(expected.platform === 2 && expected.session === 'seedsow', 'Wrong Seedsow target binding');
  const binding = Object.freeze({platform: 2, sdk: integer(expected.sdk, 'SDK base'),
    main: integer(expected.main, 'main base'), scratch: integer(expected.scratch, 'scratch base'),
    session: 'seedsow'});
  check(binding.sdk > 0 && binding.main > 0 && binding.scratch > 0, 'Empty Seedsow mapping');
  const guard = transport.guard, owned = new Map();
  let ready = false;
  const fail = reason => { throw guard.fail(reason); };
  const requireReady = () => {
    if (guard.error) throw guard.error;
    check(ready, 'Seedsow status has not been bound');
  };
  async function status() {
    try {
      const value = await transport.request('status', {maxReplyBytes: 256}, decodeStatus);
      check(value.sdk === binding.sdk && value.main === binding.main &&
        value.scratch === binding.scratch && value.session === binding.session,
      'Seedsow remote mapping differs from the immutable connection');
      ready = true;
      return value;
    } catch (error) { return fail(error); }
  }
  async function invoke(command, options, decoder) {
    requireReady(); // A pre-bind programming error cannot have dispatched native work.
    try { return await transport.request(command, options, decoder); }
    catch (error) { return fail(error); }
  }
  const arguments4 = args => {
    check(Array.isArray(args) && args.length <= 4, 'Seedsow supports at most four native arguments');
    return [0, 1, 2, 3].map(i => scalar(args[i] ?? 0, 'native argument'));
  };
  async function call(command, target, args = []) {
    const fields = arguments4(args);
    return invoke(command, {a0: scalar(target, 'call target'), a1: fields[0], a2: fields[1],
      a3: fields[2], a4: fields[3], replyBytes: 0x40}, reply =>
      ({result: reply.aux, output: reply.payload}));
  }
  async function read(start, length, timeoutMs) {
    const base = address(start, 'read address'), total = integer(length, 'read length', MAX_TRANSFER);
    const parts = [];
    for (let at = 0; at < total; at += CHUNK) {
      const size = Math.min(CHUNK, total - at);
      parts.push(await invoke('read', {a0: base + BigInt(at), a1: size, replyBytes: size,
        ...(timeoutMs === undefined ? {} : {timeoutMs})}, reply => reply.payload));
    }
    return Buffer.concat(parts);
  }
  async function write(start, bytes) {
    const base = address(start, 'write address');
    check(Buffer.isBuffer(bytes) && bytes.length <= MAX_TRANSFER, 'Invalid Seedsow write bytes');
    for (let at = 0; at < bytes.length; at += CHUNK)
      await invoke('write', {a0: base + BigInt(at), payload: Buffer.from(bytes.subarray(at, at + CHUNK)),
        replyBytes: 0}, () => undefined);
  }
  async function query(start) {
    // QueryMemory begins at zero when enumerating the whole application map.
    // Zero is invalid for dereferences, but valid as a query cursor.
    const base = scalar(start, 'query address');
    check(base < 1n << 39n, 'Invalid Seedsow query address');
    return invoke('query', {a0: base, replyBytes: 64}, reply => {
      const at = i => safeNumber(reply.payload.readBigUInt64LE(i * 8), 'memory query field');
      return {result: Number(reply.aux), address: at(0), size: at(1), type: at(2),
        attributes: at(3), permissions: at(4), ipcReferences: at(5), deviceReferences: at(6)};
    });
  }
  async function verifyFunction(start, expectedBytes) {
    const base = address(start, 'verify address');
    const bytes = Buffer.isBuffer(expectedBytes) ? Buffer.from(expectedBytes) :
      typeof expectedBytes === 'string' && /^(?:[0-9a-f]{2})+$/i.test(expectedBytes) ?
        Buffer.from(expectedBytes, 'hex') : null;
    check(bytes && bytes.length >= 4 && bytes.length <= 256, 'Invalid Seedsow expected code bytes');
    return invoke('verify', {a0: base, payload: Buffer.from(bytes.toString('hex')), replyBytes: 0}, reply => {
      check(reply.aux === 0n, 'Seedsow expected native code differs');
      return {verified: true};
    });
  }
  async function startWorkers(workers) {
    const {payload, specs} = frameWorkers(workers);
    return invoke('workersStart', {payload, maxReplyBytes: CHUNK}, reply => decodeStarted(reply, specs, owned));
  }
  async function joinWorkers(workers) {
    check(Array.isArray(workers) && workers.length > 0 && workers.length <= MAX_WORKERS,
      'Invalid Seedsow worker join count');
    const selected = workers.map(worker => {
      const index = integer(worker?.index, 'owned worker index', 0xffffffff);
      const ownedWorker = owned.get(String(index));
      check(ownedWorker, 'Seedsow worker is not owned by this transport');
      return ownedWorker;
    });
    check(new Set(selected).size === selected.length, 'Duplicate Seedsow worker join');
    const payload = Buffer.alloc(8 + selected.length * 4);
    payload.writeUInt32LE(selected.length, 0);
    selected.forEach((worker, i) => payload.writeUInt32LE(worker.index, 8 + i * 4));
    return invoke('workersJoin', {payload, maxReplyBytes: CHUNK}, reply => decodeJoined(reply, selected, owned));
  }
  async function ping(token = 1) {
    const value = scalar(token, 'ping token');
    return invoke('ping', {a0: value, replyBytes: 0}, reply => reply.aux === value);
  }
  async function say(message) {
    check(typeof message === 'string', 'Invalid Seedsow console text');
    const payload = Buffer.from(message, 'utf8');
    check(payload.length <= 1024, 'Seedsow console text exceeds bound');
    return invoke('say', {payload, replyBytes: 0}, () => ({said: true}));
  }
  return Object.freeze({binding, guard, status, call: (offset, args) => call('call', offset, args),
    callAddress: (target, args) => call('callAddress', target, args), read, write, query,
    verifyFunction, startWorkers, joinWorkers, ping, say,
    get output() { return binding.scratch + 0x4040; }});
}

module.exports = {create, CHUNK, MAX_TRANSFER};
