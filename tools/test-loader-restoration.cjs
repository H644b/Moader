#!/usr/bin/env node
'use strict';
// Offline behavioral fixtures for the actual upstream BaseGate. No acquisition,
// transport, native binary, kernel scheduler, or console behavior is exercised.
const assert = require('node:assert/strict');
const net = require('node:net'), http = require('node:http'), https = require('node:https');
let forbidden = 0;
const deny = () => { forbidden++; throw Error('Loader restoration tests forbid network/native execution'); };
net.Socket.prototype.connect = net.Server.prototype.listen = deny;
http.request = http.get = https.request = https.get = deny;
require('node:tls').connect = deny;
const cp = require('node:child_process');
for (const name of ['spawn', 'spawnSync', 'exec', 'execSync', 'execFile', 'execFileSync', 'fork']) cp[name] = deny;
globalThis.fetch = deny;
const gateModule = require('../research/sources/ns2jb/engine/switch2/ro/gate.cjs');
const BaseGate = gateModule.Gate;
const {withGateRestoration} = require('./loader-restoration.cjs');
const {openObserver} = require('./loader-observer.cjs');
const {createGuard} = require('./seedsow-transport.cjs');
let passed = 0;

function fixture(options = {}) {
  const server = 0x1750000000, waiterBase = 0x1750010000, request = 0x1750020000;
  const waiters = Array.from({length: 7}, (_, i) => waiterBase + i * 0x110);
  const session = Buffer.alloc(0x88), waiterBytes = Buffer.alloc(7 * 0x110), requestBytes = Buffer.alloc(0x100);
  const putPointer = (at, n) => session.writeBigUInt64LE(BigInt(n), at);
  putPointer(0, 0x1712345678); putPointer(0x28, waiters[2]); session.writeUInt32LE(31, 0x30);
  putPointer(0x38, 1); session.writeUInt32LE(1, 0x40); session.writeUInt32LE(3, 0x44);
  session.writeUInt32LE(25, 0x48); putPointer(0x60, request); putPointer(0x68, 0x100);
  putPointer(0x70, 0x1750030000); putPointer(0x78, 0x1712345990);
  requestBytes.writeUInt32LE(options.marker ? 0xffffffff : 6);
  if (options.selected) waiterBytes.writeBigUInt64LE(BigInt(server), 5 * 0x110 + 0x10);
  if (options.tag !== undefined) putPointer(0x38, options.tag);
  if (options.state !== undefined) session.writeUInt32LE(options.state, 0x40);
  const row = {address: server, request, domain: Number(session.readBigUInt64LE(0x70)),
    metadata: Number(session.readBigUInt64LE(0x78)), receive: 25, wait: 25, raw: session.toString('hex')};
  const event = {sourceHandle: 31, read: 41, write: 42};
  let t = 0, signaled = !!options.eventAlreadySet, signalAt = 0, latch = false, pending = false;
  let readCount = 0, signalCount = 0, clearCount = 0;
  const trace = [], blocks = [];
  const segment = (at, length) => {
    for (const [base, bytes] of [[server, session], [waiterBase, waiterBytes], [request, requestBytes]])
      if (at >= base && at + length <= base + bytes.length) return bytes.subarray(at - base, at - base + length);
    throw Error('Fixture read outside synthetic memory');
  };
  const guard = () => assert(!latch && !pending, 'A native call followed the shared stop latch/pending request');
  const record = (kind, extra = {}) => { guard(); trace.push({kind, signaled, t, ...extra}); };
  const memory = {
    async read(at, length) {
      record('retained-read', {at, length}); assert(!signaled, 'Ordinary FS read while event is asserted');
      return Buffer.from(segment(at, length));
    },
    async write(at, expected, replacement) {
      record('checked-write', {at, expected: expected.toString('hex'), replacement: replacement.toString('hex')});
      assert(!signaled, 'Retained FS write while event is asserted');
      assert.deepEqual(await this.read(at, expected.length), expected);
      if (options.writeFailure === at) {
        pending = true;
        throw Object.assign(Error('write timeout'), {nativeRequestUncertain: true});
      }
      replacement.copy(segment(at, replacement.length));
      assert.deepEqual(await this.read(at, replacement.length), replacement);
    },
    async request(command) {
      record(command.command, {handle: command.handle});
      if (command.command === 'root-native-wait-zero') {
        assert.equal(command.handle, event.read); return {result: signaled ? 0 : 0xea01};
      }
      if (command.command === 'root-native-event-signal') {
        assert.equal(command.handle, event.write); signalCount++;
        if (options.signalTimeout) {
          pending = true; throw Object.assign(Error('signal timeout'), {nativeRequestUncertain: true});
        }
        assert.equal(signalCount, 1); signaled = true; signalAt = t;
        return {result: options.signalResult || 0, at: 'synthetic-signal'};
      }
      if (command.command === 'root-native-event-clear') {
        assert.equal(command.handle, event.read); assert.equal(++clearCount, 1);
        if (options.clearTimeout) {
          pending = true; throw Object.assign(Error('clear timeout'), {nativeRequestUncertain: true});
        }
        if (options.clearResult) return {result: options.clearResult};
        signaled = false; return {result: 0, at: 'synthetic-clear'};
      }
      throw Error('Unexpected native request: ' + command.command);
    },
  };
  const observer = {
    get uncertain() { return latch || pending; },
    now() { return t; },
    async delay(ms) { trace.push({kind: 'delay', t, ms, signaled}); t += ms; },
    block(error) { blocks.push(error); latch = true; },
    async read(at, length) {
      record('observer-read', {at, length});
      if (signaled) {
        readCount++;
        if (options.readTimeout) {
          pending = true; throw Object.assign(Error('observer transport timeout'), {code: 'ETIMEDOUT', nativeRequestUncertain: true});
        }
        t += options.readLatency || 0;
        if (options.moveParent) putPointer(0x28, waiters[3]);
        if (options.advanceEpoch) waiterBytes.writeBigUInt64LE(BigInt(9000 + readCount), 2 * 0x110 + 0x18);
        if (options.changeIdentity !== undefined) putPointer(options.changeIdentity, 0x1712999990);
        if (options.changeWait) session.writeUInt32LE(999, 0x30);
        if (t - signalAt >= (options.receiptAfter ?? 150))
          session.writeUInt32LE(options.receiptValue ?? 3, 0x44);
        if (options.pendingOnRead) pending = true;
        if (options.shortRead) return Buffer.alloc(length - 1);
      }
      return Buffer.from(segment(at, length));
    },
  };
  let gate;
  return {memory, observer, row, event, waiters, trace, blocks, session, requestBytes, server,
    get gate() { return gate; }, get time() { return t; }, get signaled() { return signaled; },
    get signalCount() { return signalCount; }, get clearCount() { return clearCount; },
    async run(operation = value => value.restore()) {
      return withGateRestoration(gateModule, observer, async () => {
        assert.notEqual(gateModule.Gate, BaseGate);
        gate = new gateModule.Gate(memory, row, [], waiters, 55, event);
        assert(gate instanceof BaseGate); assert.equal(gate.inspect, BaseGate.prototype.inspect);
        gate.parked = true;
        if (options.outstanding) gate.outstanding = {original: options.original ?? '06000000'};
        return operation(gate);
      });
    },
  };
}
async function test(name, fn) {
  await fn(); assert.equal(gateModule.Gate, BaseGate); passed++; console.log('PASS: ' + name);
}
async function unresolved(f, pattern) {
  await assert.rejects(f.run(), error => {
    assert.match(error.message, pattern); assert.equal(error.loaderRestorationUnresolved, true); return true;
  });
  assert.equal(f.gate.restoration.unresolved, true);
  assert.equal(f.gate.restoration.snapshotRetired, false);
  assert.equal(f.gate.restoring, true); assert.equal(f.gate.restored, false);
  assert.equal(f.blocks.length, 1);
  const calls = f.trace.length;
  await assert.rejects(f.gate.restore(), pattern);
  assert.equal(f.trace.length, calls, 'restore replay issued another native call');
}

async function main() {
  await test('real BaseGate; delayed wake beyond 100ms requires receipt before one clear', async () => {
    const f = fixture({receiptAfter: 200});
    const value = await f.run(async gate => {
      await gate.inspect(true); // Exercise the inherited real BaseGate validation.
      await gate.restore(); await gate.restore(); return 17;
    });
    assert.equal(value, 17); assert(f.time >= 200); assert.equal(f.signalCount, 1); assert.equal(f.clearCount, 1);
    assert.equal(f.gate.restoration.receiptSeen, true); assert.equal(f.gate.restoration.snapshotRetired, true);
    assert.equal(f.gate.parked, false); assert.equal(f.gate.restoring, false); assert.equal(f.gate.restored, true);
    assert.equal(f.blocks.length, 0);
    const writes = f.trace.filter(x => x.kind === 'checked-write');
    assert.deepEqual(writes.map(x => [x.at, x.expected, x.replacement]),
      [[f.server + 0x44, '03', 'ff'], [f.server + 0x30, '1f000000', '19000000']]);
    assert(f.trace.filter(x => x.signaled && x.kind.includes('read')).every(x => x.kind === 'observer-read'));
    const clear = f.trace.find(x => x.kind === 'root-native-event-clear'); assert(clear.t >= 200);
  });
  await test('low-byte receive results 0..3 overwrite the cookie', async () => {
    for (const receiptValue of [0, 1, 2, 3, 0xea03]) {
      const f = fixture({receiptAfter: 0, receiptValue}); await f.run();
      assert.equal(f.gate.restoration.lastObservation.receipt, receiptValue & 0xff);
    }
  });
  await test('unrelated epoch advance and parent movement never acknowledge unchanged cookie', async () => {
    const f = fixture({receiptAfter: Infinity, advanceEpoch: true, moveParent: true});
    await unresolved(f, /receipt deadline expired/);
    assert.equal(f.gate.restoration.observations, 30); assert.equal(f.clearCount, 1);
    assert.equal(f.gate.restoration.pollingExpired, true); assert.equal(f.signaled, false);
  });
  await test('independent observation latency is included in the polling deadline', async () => {
    const f = fixture({readLatency: 300, receiptAfter: 850});
    await unresolved(f, /receipt deadline expired/);
    assert.equal(f.gate.restoration.observations, 3); assert(f.time >= 750);
    assert.equal(f.gate.restoration.receiptSeen, undefined); assert.equal(f.clearCount, 1);
    assert.equal(f.gate.restoration.receiptObserved.withinDeadline, false);
    assert.equal(f.gate.restoration.receiptObserved.value, 3);
  });
  await test('default real clock and timers enforce a finite unchanged-cookie polling budget', async () => {
    const f = fixture({receiptAfter: Infinity}); delete f.observer.now; delete f.observer.delay;
    await unresolved(f, /receipt deadline expired/);
    assert(f.gate.restoration.observations > 0 && f.gate.restoration.observations <= 30);
    assert.equal(f.gate.restoration.deadlineMs, 750); assert.equal(f.clearCount, 1);
  });
  await test('real independent read latency can consume the deadline; late receipt stays visible', async () => {
    const f = fixture({receiptAfter: Infinity}); delete f.observer.now; delete f.observer.delay;
    const read = f.observer.read.bind(f.observer); let observations = 0;
    f.observer.read = async (at, length) => {
      const bytes = await read(at, length);
      if (f.signaled) {
        await new Promise(resolve => setTimeout(resolve, 800));
        observations++;
        bytes.writeUInt32LE(3, 0x44);
      }
      return bytes;
    };
    await unresolved(f, /receipt deadline expired/);
    assert.equal(observations, 1);
    assert(f.gate.restoration.lastObservation.elapsedMs >= 750);
    assert.equal(f.gate.restoration.receiptObserved.withinDeadline, false);
    assert.equal(f.gate.restoration.receiptSeen, undefined); assert.equal(f.clearCount, 1);
  });
  await test('identity and wait mismatches after wake cause no cleanup calls', async () => {
    for (const options of [{changeIdentity: 0}, {changeIdentity: 0x70}, {changeIdentity: 0x78},
      {changeIdentity: 0x60}, {changeIdentity: 0x68}, {changeWait: true}]) {
      const f = fixture(options);
      await unresolved(f, /identity changed|wait handle changed/); assert.equal(f.clearCount, 0);
    }
  });
  await test('selected waiter, nonidle state, tag mismatch and asserted event fail before writes', async () => {
    for (const options of [{selected: true}, {state: 2}, {tag: 2}, {eventAlreadySet: true}]) {
      const f = fixture(options); await unresolved(f, /selected waiter|not idle|tag is not 1|must be unsignaled/);
      assert.equal(f.trace.filter(x => x.kind === 'checked-write').length, 0); assert.equal(f.signalCount, 0);
    }
  });
  await test('outstanding marker rollback precedes cookie and wait publication', async () => {
    const f = fixture({marker: true, outstanding: true}); await f.run();
    assert.equal(f.requestBytes.readUInt32LE(), 6); assert.equal(f.gate.outstanding, undefined);
    assert.equal(f.gate.restoration.requestMarkerRolledBack, true);
    assert.deepEqual(f.trace.filter(x => x.kind === 'checked-write').map(x => x.at),
      [f.row.request, f.server + 0x44, f.server + 0x30]);
    const received = fixture({outstanding: true}); await received.run();
    assert.equal(received.gate.restoration.requestMarkerRolledBack, undefined);
  });
  await test('unknown marker or invalid rollback header fails without guessed writes', async () => {
    for (const options of [{marker: true}, {marker: true, outstanding: true, original: 'ffffffff'},
      {marker: true, outstanding: true, original: 'bad'}]) {
      const f = fixture(options); await unresolved(f, /marker cannot|valid original/);
      assert.equal(f.trace.filter(x => x.kind === 'checked-write').length, 0);
    }
  });
  await test('clear failure preserves receipt evidence but never marks restoration complete', async () => {
    const f = fixture({receiptAfter: 0, clearResult: 0xf601}); await unresolved(f, /event did not clear/);
    assert.equal(f.gate.restoration.receiptSeen, true); assert.equal(f.clearCount, 1); assert.equal(f.signaled, true);
  });
  await test('definite signal failure is unresolved without a speculative clear', async () => {
    const f = fixture({signalResult: 0xf601}); await unresolved(f, /event did not signal/);
    assert.equal(f.signalCount, 1); assert.equal(f.clearCount, 0);
  });
  await test('transport timeouts prohibit clear, retries and every later native call', async () => {
    for (const options of [{readTimeout: true}, {signalTimeout: true}, {clearTimeout: true, receiptAfter: 0},
      {writeFailure: 0x1750000044}, {writeFailure: 0x1750000030}]) {
      const f = fixture(options); await unresolved(f, /timeout/);
      assert.equal(f.clearCount, options.clearTimeout ? 1 : 0);
      assert.equal(f.blocks[0].nativeRequestUncertain, true);
    }
  });
  await test('malformed observation is unresolved and triggers no native cleanup', async () => {
    const f = fixture({shortRead: true}); await unresolved(f, /invalid length/); assert.equal(f.clearCount, 0);
  });
  await test('pending latch despite a returned observation prevents event clear', async () => {
    const f = fixture({pendingOnRead: true, receiptAfter: 0});
    await unresolved(f, /blocked by an unresolved/); assert.equal(f.clearCount, 0);
  });
  await test('receipt outside 0..3 never counts as restoration', async () => {
    for (const receiptValue of [4, 0xfe]) {
      const f = fixture({receiptAfter: 0, receiptValue}); await unresolved(f, /receipt deadline expired/);
      assert.equal(f.gate.restoration.receiptSeen, undefined); assert.equal(f.clearCount, 1);
    }
  });
  await test('a stale cookie is rejected before any write', async () => {
    const f = fixture(); f.session.writeUInt32LE(0xff, 0x44);
    await unresolved(f, /known pre-cookie result/);
    assert.equal(f.trace.filter(x => x.kind === 'checked-write').length, 0);
  });
  await test('unsupported gate and missing/uncertain observer fail before operation', async () => {
    let invoked = false; const operation = () => { invoked = true; };
    await assert.rejects(withGateRestoration({}, fixture().observer, operation), /Unsupported/);
    await assert.rejects(withGateRestoration({Gate: () => {}}, fixture().observer, operation), /Unsupported/);
    await assert.rejects(withGateRestoration(gateModule, {}, operation), /independent observer/);
    const f = fixture(); f.observer.block(Error('prior failure'));
    await assert.rejects(withGateRestoration(gateModule, f.observer, operation), /already uncertain/);
    assert.equal(invoked, false); assert.equal(f.trace.length, 0);
  });
  await test('non-event restore remains the inherited implementation', async () => {
    const f = fixture(); let called = false;
    const original = BaseGate.prototype.restore;
    BaseGate.prototype.restore = async function (...args) { called = true; assert.deepEqual(args, [19]); return 23; };
    try {
      await withGateRestoration(gateModule, f.observer, async () => {
        const gate = new gateModule.Gate(f.memory, f.row, [{sourceHandle: 31}], f.waiters, 55);
        assert.equal(await gate.restore(19), 23);
      });
      assert.equal(called, true);
    } finally { BaseGate.prototype.restore = original; }
  });
  await test('module export restores on operation error, nested wrappers and swallowed gate failure', async () => {
    const f = fixture();
    await assert.rejects(withGateRestoration(gateModule, f.observer, async () => {
      await assert.rejects(withGateRestoration(gateModule, f.observer, () => {}), /already active/);
      throw Error('operation failed');
    }), /operation failed/);
    const failed = fixture({selected: true});
    await assert.rejects(failed.run(async gate => { try { await gate.restore(); } catch {} return 'not success'; }), /selected waiter/);
  });
  await test('concurrent restore reentry blocks both attempts before another native call', async () => {
    const f = fixture();
    await assert.rejects(f.run(async gate => {
      const results = await Promise.allSettled([gate.restore(), gate.restore()]);
      assert(results.every(result => result.status === 'rejected'));
      assert.equal(f.trace.length, 1);
    }), /already in progress/);
    assert.equal(f.signalCount, 0); assert.equal(f.clearCount, 0);
  });
  await test('independent observer latches the first cause through a getter-only transport guard', async () => {
    const guard = createGuard(), record = {}, memory = {info: {session: 'seedsow'},
      reportFile: '/synthetic/current-fs-report.json'};
    let closeCount = 0, saves = 0;
    const client = {pid: 209n, file: '/synthetic/ro-protocol.json', pending: false,
      record: {session: 'seedsow', report: memory.reportFile},
      async fsProcess() { return 27; },
      async info(handle) {
        assert.equal(handle, 27);
        return {result: 0, programId: '0400000000000000', pid: '81'};
      },
      async close() { closeCount++; },
    };
    const observer = await openObserver({run: '/synthetic/current-run', memory,
      execution: {pid: '209'}, state: guard, record, save: async () => { saves++; }},
    {openClient: async () => client});
    const first = Error('receipt missing'), second = Error('later failure');
    assert.equal(observer.block(first), first);
    assert.equal(observer.block(second), first);
    assert.equal(guard.error, first);
    assert.equal(first.nativeRequestUncertain, true);
    assert.equal(observer.uncertain, true);
    assert.equal(record.resourcesRetained, true);
    assert.equal(record.unresolved, true);
    assert.match(record.error, /receipt missing/);
    await observer.close();
    assert.equal(closeCount, 0);
    assert(saves >= 2);
  });
  assert.equal(forbidden, 0);
  console.log(`${passed} offline Loader restoration checks passed.`);
}
main().catch(error => { console.error(error.stack); process.exitCode = 1; });
