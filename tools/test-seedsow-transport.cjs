// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Entirely synthetic streams. No socket, console, upstream module or /eval.
const assert = require('node:assert/strict');
const {test} = require('node:test');
const {EventEmitter} = require('node:events');
let forbidden = 0;
const deny = () => { ++forbidden; throw Error('Tests forbid network IO'); };
require('node:net').createConnection = deny;
require('node:http').request = deny;
require('node:https').request = deny;
const {create, createGuard, COMMANDS, LIMIT, MAX_QUEUED} = require('./seedsow-transport.cjs');

class Socket extends EventEmitter {
  readable = true;
  writable = true;
  readableLength = 0;
  connecting = false;
  destroyed = false;
  writes = [];
  paused = false;
  constructor(handler = () => {}) { super(); this.handler = handler; }
  write(body, done) {
    this.writes.push(Buffer.from(body));
    queueMicrotask(() => this.handler(body, done, this));
    return true;
  }
  pause() { this.paused = true; }
  destroy() { throw Error('Transport must preserve owned socket on uncertainty'); }
  end() { throw Error('Transport must not end the owned socket'); }
}

// Independent wire fixture: 24-byte header, little-endian magic/status/length,
// signed auxiliary result followed by payload. No production encoder is used.
function reply(payload = Buffer.alloc(0), {status = 0, aux = 0n, length = BigInt(payload.length)} = {}) {
  const out = Buffer.alloc(24 + payload.length);
  out.writeUInt32LE(0x5248324e, 0);
  out.writeUInt32LE(status, 4);
  out.writeBigUInt64LE(length, 8);
  out.writeBigInt64LE(aux, 16);
  payload.copy(out, 24);
  return out;
}
const tick = () => new Promise(resolve => setImmediate(resolve));

async function stopped(client, socket, expectedWrites = 1) {
  const original = client.guard.error;
  assert.ok(original?.nativeRequestUncertain);
  await assert.rejects(client.request('ping'), error => error === original);
  let endpointCalls = 0;
  await assert.rejects(client.guard.run(() => { ++endpointCalls; }), error => error === original);
  assert.equal(endpointCalls, 0);
  assert.equal(socket.writes.length, expectedWrites);
  assert.equal(socket.paused, true);
  assert.equal(socket.destroyed, false);
}

test('import and creation perform no IO; socket ownership cannot be reused', () => {
  const socket = new Socket();
  const client = create(socket);
  assert.equal(forbidden, 0);
  assert.equal(socket.writes.length, 0);
  assert.equal(client.guard.error, null);
  assert.throws(() => create(socket), /exclusively owned/);
  for (const properties of [{connecting: true}, {destroyed: true}, {readableLength: 1}, {writable: false}])
    assert.throws(() => create(Object.assign(new Socket(), properties)), /already-connected/);
  const observed = new Socket(); observed.on('data', () => {});
  assert.throws(() => create(observed), /exclusively owned/);
});

test('64-byte request geometry and u64 values match the pinned console protocol', async () => {
  const socket = new Socket((body, done, stream) => {
    assert.equal(body.length, 67);
    assert.equal(body.readUInt32LE(0), 0x5248324e);
    assert.equal(body.readUInt32LE(4), 3);
    assert.equal(body.readBigUInt64LE(8), 0x100000010n);
    assert.equal(body.readBigUInt64LE(16), 0xffffffffffffffffn);
    assert.equal(body.readBigUInt64LE(24), 2n);
    assert.equal(body.readBigUInt64LE(32), 3n);
    assert.equal(body.readBigUInt64LE(40), 4n);
    assert.equal(body.readUInt32LE(48), 3);
    assert.deepEqual(body.subarray(52, 64), Buffer.alloc(12));
    assert.equal(body.subarray(64).toString(), 'abc');
    done(); stream.emit('data', reply(Buffer.from('result'), {aux: -1n}));
  });
  const client = create(socket);
  const result = await client.request('callAddress', {a0: 0x100000010n, a1: 0xffffffffffffffffn,
    a2: '2', a3: 3, a4: 4, payload: Buffer.from('abc')});
  assert.equal(result.aux, -1n);
  assert.equal(result.payload.toString(), 'result');
});

test('fragmented headers/payloads are assembled once, including zero-length replies', async () => {
  const socket = new Socket((body, done, stream) => {
    const bytes = reply(body.readUInt32LE(4) === 4 ? Buffer.from('data') : Buffer.alloc(0));
    for (const byte of bytes) stream.emit('data', Buffer.from([byte]));
    done();
  });
  const client = create(socket);
  assert.equal((await client.request('read', {replyBytes: 4})).payload.toString(), 'data');
  assert.equal((await client.request('write', {replyBytes: 0})).payload.length, 0);
  assert.equal(client.guard.error, null);
});

test('one acknowledged stop permits the expected EOF without replay', async () => {
  const socket = new Socket((body, done, stream) => {
    assert.equal(body.readUInt32LE(4), 11);
    done();
    stream.emit('data', reply());
    queueMicrotask(() => { stream.emit('end'); stream.emit('close'); });
  });
  const client = create(socket);
  const result = await client.request('stop', {replyBytes: 0}, response => {
    assert.equal(response.aux, 0n);
    return true;
  });
  await tick();
  assert.equal(result, true);
  assert.equal(client.guard.error, null);
  assert.equal(socket.writes.length, 1);
});

test('EOF before stop acknowledgement preserves the uncertain owner', async () => {
  const socket = new Socket((body, done, stream) => {
    assert.equal(body.readUInt32LE(4), 11);
    done(); stream.emit('end');
  });
  const client = create(socket);
  await assert.rejects(client.request('stop', {replyBytes: 0}), /socket end/);
  await stopped(client, socket);
});

test('queued bytes are immutable and only one command is in flight', async () => {
  const socket = new Socket();
  const client = create(socket);
  const first = client.request('ping');
  const bytes = Buffer.from('original'), options = {a0: 123, payload: bytes};
  const second = client.request('write', options);
  bytes.fill(0); options.a0 = 456;
  await tick(); assert.equal(socket.writes.length, 1);
  socket.emit('data', reply()); await first;
  await tick(); assert.equal(socket.writes.length, 2);
  assert.equal(socket.writes[1].subarray(64).toString(), 'original');
  assert.equal(socket.writes[1].readBigUInt64LE(8), 123n);
  socket.emit('data', reply()); await second;
});

for (const event of ['error', 'end', 'close']) test('lost native reply (' + event + ') never resends or releases ownership', async () => {
  let executed = 0;
  const socket = new Socket((body, done, stream) => {
    ++executed; done(); stream.emit(event, Error('ECONNRESET after execution'));
  });
  const client = create(socket);
  const tasks = [client.request('workersStart'), client.request('workersJoin'), client.request('write')];
  const outcomes = await Promise.allSettled(tasks);
  assert.equal(executed, 1);
  assert.ok(outcomes.every(value => value.status === 'rejected' && value.reason === client.guard.error));
  await stopped(client, socket);
});

for (const [name, bytes, options] of [
  ['bad magic', Buffer.alloc(24), {}],
  ['huge declared u64 length', reply(Buffer.alloc(0), {length: 0xffffffffffffffffn}), {}],
  ['per-request reply limit', reply(Buffer.alloc(2)), {maxReplyBytes: 1}],
  ['wrong exact length', reply(Buffer.alloc(2)), {replyBytes: 1}],
  ['duplicate reply', Buffer.concat([reply(), reply()]), {}],
  ['native worker error', reply(Buffer.from('worker still running'), {status: 1}), {}],
]) test(name + ' latches before queued worker/cleanup activity', async () => {
  const socket = new Socket((body, done, stream) => { done(); stream.emit('data', bytes); });
  const client = create(socket);
  const result = await Promise.allSettled([client.request('workersJoin', options), client.request('verify')]);
  assert.ok(result.every(value => value.status === 'rejected'));
  await stopped(client, socket);
});

test('partial-frame deadline latches once and rejects late replies and queued calls', async () => {
  const socket = new Socket((body, done, stream) => { done(); stream.emit('data', reply().subarray(0, 12)); });
  const client = create(socket);
  const outcomes = await Promise.allSettled([client.request('call', {timeoutMs: 10}), client.request('write')]);
  assert.ok(outcomes.every(value => value.status === 'rejected' && /deadline/.test(value.reason.message)));
  socket.emit('data', reply());
  await stopped(client, socket);
});

test('write callback and synchronous write failures share the uncertainty latch', async () => {
  for (const sync of [false, true]) {
    const socket = new Socket((body, done) => done(Error('EPIPE')));
    if (sync) socket.write = () => { socket.writes.push(Buffer.alloc(0)); throw Error('EPIPE'); };
    const client = create(socket);
    await assert.rejects(client.request('write'), /EPIPE/);
    await stopped(client, socket);
  }
});

test('higher-level worker decode failure stops the queue before the next send', async () => {
  const socket = new Socket((body, done, stream) => { done(); stream.emit('data', reply(Buffer.from('short'))); });
  const client = create(socket);
  const tasks = [client.request('workersStart', {}, () => { throw Error('Invalid worker ownership reply'); }),
    client.request('workersJoin')];
  await Promise.allSettled(tasks);
  assert.match(client.guard.error.message, /ownership reply/);
  await stopped(client, socket);
});

test('shared endpoint uncertainty aborts an active socket command and all other transports', async () => {
  const guard = createGuard(), a = new Socket(), b = new Socket();
  const clientA = create(a, guard), clientB = create(b, guard);
  const tasks = [clientA.request('call'), clientB.request('workersJoin')];
  await tick();
  const cause = guard.fail(Error('Retained endpoint reply was lost'));
  const results = await Promise.allSettled(tasks);
  assert.ok(results.every(value => value.status === 'rejected' && value.reason === cause));
  await stopped(clientA, a);
  await stopped(clientB, b, 0);
  assert.equal(guard.fail(Error('later failure')), cause);
});

test('a failing reporting callback cannot prevent another transport from stopping', async () => {
  const guard = createGuard(); guard.subscribe(() => { throw Error('journal unavailable'); });
  const socket = new Socket(), client = create(socket, guard);
  const task = client.request('read'); await tick();
  guard.fail(Error('original transport failure'));
  await assert.rejects(task, /original transport failure/);
  await stopped(client, socket);
});

test('idle disconnect and unsolicited bytes quarantine before the first command', async () => {
  for (const event of ['close', 'data']) {
    const socket = new Socket(), client = create(socket);
    socket.emit(event, reply());
    await stopped(client, socket, 0);
  }
});

test('invalid scalar/bounds and unsupported eval/shutdown/six-argument commands fail without sending', async () => {
  for (const [command, options] of [
    ['run', {}], ['shutdown', {}], ['callAddress6', {}], ['callAddress', {a5: 1}],
    ['call', {a0: Number.MAX_SAFE_INTEGER + 1}], ['call', {a0: -1}],
    ['call', {a0: 1n << 64n}], ['call', {a0: {u32: 10}}],
    ['write', {payload: Buffer.alloc(LIMIT + 1)}], ['read', {maxReplyBytes: LIMIT + 1}],
    ['read', {replyBytes: -1}], ['read', {timeoutMs: 0}], ['read', {timeoutMs: Infinity}],
  ]) {
    const socket = new Socket(), client = create(socket);
    await assert.rejects(client.request(command, options));
    await stopped(client, socket, 0);
  }
});

test('queue is bounded and overflow stops every queued operation before dispatch', async () => {
  const socket = new Socket(), client = create(socket);
  const tasks = Array.from({length: MAX_QUEUED + 1}, () => client.request('ping'));
  const results = await Promise.allSettled(tasks);
  assert.ok(results.every(value => value.status === 'rejected'));
  assert.match(client.guard.error.message, /queue exceeds/);
  await stopped(client, socket, 0);
});

test('Seedsow provider still refuses live admission after transport implementation', () => {
  const profile = require('./seedsow-provider.cjs');
  assert.throws(() => profile.qualifyRun(), /not admitted yet/);
  assert.equal(COMMANDS.workersStart, 8);
  assert.equal(COMMANDS.workersJoin, 9);
  assert.equal(forbidden, 0);
});
