// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Independent protocol fixtures only. No console connection or upstream import.
const assert = require('node:assert/strict');
const {test} = require('node:test');
const {EventEmitter} = require('node:events');
let network = 0;
const deny = () => { ++network; throw Error('Fixture forbids network IO'); };
require('node:net').createConnection = deny;
require('node:http').request = deny;
require('node:https').request = deny;
const transport = require('./seedsow-transport.cjs');
const typed = require('./seedsow-rpc.cjs');

class Socket extends EventEmitter {
  readable = true; writable = true; readableLength = 0; connecting = false; destroyed = false;
  writes = []; paused = false;
  constructor(handler) { super(); this.handler = handler; }
  write(bytes, done) {
    this.writes.push(Buffer.from(bytes));
    queueMicrotask(() => this.handler(Buffer.from(bytes), this, done));
    return true;
  }
  pause() { this.paused = true; }
  end() { throw Error('Owned socket must remain retained'); }
  destroy() { throw Error('Owned socket must remain retained'); }
}
function reply(payload = Buffer.alloc(0), aux = 0n, status = 0) {
  const result = Buffer.alloc(24 + payload.length);
  result.writeUInt32LE(0x5248324e, 0);
  result.writeUInt32LE(status, 4);
  result.writeBigUInt64LE(BigInt(payload.length), 8);
  result.writeBigInt64LE(aux, 16);
  payload.copy(result, 24);
  return result;
}
const binding = {platform: 2, sdk: 0x1000000000, main: 0x2000000000,
  scratch: 0x3000000000, session: 'seedsow'};
const status = Buffer.from(`S\0${binding.sdk}\0${binding.main}\0${binding.scratch}\0seedsow`);
function fixture(handler, expected = binding) {
  const socket = new Socket((body, stream, done) => {
    done();
    if (body.readUInt32LE(4) === 1) stream.emit('data', reply(status));
    else handler(body, stream);
  });
  return {socket, rpc: typed.create(transport.create(socket), expected)};
}
function u64(...words) {
  const out = Buffer.alloc(words.length * 8);
  words.forEach((word, i) => out.writeBigUInt64LE(BigInt(word), i * 8));
  return out;
}

test('status binds exact immutable mapping before native calls', async () => {
  const {socket, rpc} = fixture((body, stream) => stream.emit('data', reply()));
  assert.throws(() => typed.create(transport.create(new Socket()), {...binding, platform: 1}), /target binding/);
  await assert.rejects(rpc.call(0x123, []), /not been bound/);
  // A rejected unbound call is local; status can still establish the binding.
  assert.equal((await rpc.status()).session, 'seedsow');
  assert.equal(rpc.output, binding.scratch + 0x4040);
  assert.equal(await rpc.ping(7), false);
  assert.equal(socket.writes.length, 2);
  assert.equal(network, 0);
});

test('remote mapping drift latches the owned transport before a native send', async () => {
  const {socket, rpc} = fixture(() => {}, {...binding, main: binding.main + 0x1000});
  await assert.rejects(rpc.status(), /mapping differs/);
  assert.equal(socket.writes.length, 1);
  assert.ok(rpc.guard.error?.nativeRequestUncertain);
  await assert.rejects(rpc.read(0x1000, 8), error => error === rpc.guard.error);
  assert.equal(socket.paused, true);
});

test('four scalar slots, exact call output and chunked read/write have pinned wire shape', async () => {
  const seen = [];
  const {socket, rpc} = fixture((body, stream) => {
    const command = body.readUInt32LE(4); seen.push(command);
    if (command === 2 || command === 3) stream.emit('data', reply(Buffer.alloc(64, 0xa5), -5n));
    else if (command === 4) stream.emit('data', reply(Buffer.alloc(Number(body.readBigUInt64LE(16)), 0x6c)));
    else stream.emit('data', reply());
  });
  await rpc.status();
  const call = await rpc.call(0x1234, [1, 2, 3, 4]);
  assert.equal(call.result, -5n);
  assert.equal(call.output.length, 64);
  assert.deepEqual([8, 16, 24, 32, 40].map(at => socket.writes[1].readBigUInt64LE(at)),
    [0x1234n, 1n, 2n, 3n, 4n]);
  await assert.rejects(rpc.callAddress(0x5000, [1, 2, 3, 4, 5]), /at most four/);
  const addressCall = await rpc.callAddress(0x5000, [9]);
  assert.equal(addressCall.result, -5n);
  const data = await rpc.read(0x7000, typed.CHUNK + 3);
  assert.equal(data.length, typed.CHUNK + 3);
  assert.ok(data.every(byte => byte === 0x6c));
  await rpc.write(0x9000, Buffer.alloc(typed.CHUNK + 3, 0x45));
  assert.deepEqual(seen, [2, 3, 4, 4, 5, 5]);
  assert.equal(socket.writes.at(-1).readBigUInt64LE(8), BigInt(0x9000 + typed.CHUNK));
  assert.equal(socket.writes.at(-1).readUInt32LE(48), 3);
  assert.equal(network, 0);
});

test('query and verify reject malformed or mismatching native results', async () => {
  const {rpc} = fixture((body, stream) => {
    const command = body.readUInt32LE(4);
    stream.emit('data', command === 6 ? reply(u64(0x1000, 0x4000, 3, 4, 5, 6, 7, 8)) : reply(Buffer.alloc(0), 1n));
  });
  await rpc.status();
  assert.equal((await rpc.query(0)).address, 0x1000);
  assert.deepEqual(await rpc.query(0x1000), {result: 0, address: 0x1000, size: 0x4000,
    type: 3, attributes: 4, permissions: 5, ipcReferences: 6, deviceReferences: 7});
  await assert.rejects(rpc.verifyFunction(0x1000, Buffer.alloc(4, 0xab)), /code differs/);
  assert.ok(rpc.guard.error?.nativeRequestUncertain);
});

function startedPayload({badIndex = false, truncated = false} = {}) {
  const data = Buffer.alloc(truncated ? 64 : 8 + 56 + 16 + 4);
  data.writeUInt32LE(1, 0);
  data.writeBigUInt64LE(0x1234n, 8); // thread
  data.writeBigUInt64LE(0x9000n, 16); // bytes
  data.writeBigUInt64LE(0x8008n, 24); // final result offset
  data.writeBigUInt64LE(0x400080n, 32); // cancel cell
  data.writeBigUInt64LE(0xfedcba9876543210n, 40); // start cell
  data.writeBigUInt64LE(badIndex ? 0n : 1n, 48);
  data.writeUInt32LE(2, 56); // one dynamic arg, one native call
  data.writeUInt32LE(1, 60);
  if (!truncated) {
    data.writeBigUInt64LE(0x8000n, 64);
    data.writeBigUInt64LE(0x8008n, 72);
    data.writeUInt32LE(1, 80);
  }
  return data;
}
const worker = {base: 0x400000, limit: 0x10000, slot: 0x3fff00, calls: [
  {function: 0x500000, args: [{u32: 7}, 8], name: 'commit', kind: 'ipc'}]};
test('worker start and join validate ownership, metadata and result geometry', async () => {
  const {socket, rpc} = fixture((body, stream) => {
    const command = body.readUInt32LE(4);
    if (command === 8) stream.emit('data', reply(startedPayload(), 1n));
    else if (command === 9) {
      assert.equal(body.subarray(64).readUInt32LE(8), 1);
      const data = Buffer.concat([Buffer.from([1, 0, 0, 0, 0, 0, 0, 0]), u64(44, 1, 55)]);
      stream.emit('data', reply(data, 1n));
    }
  });
  await rpc.status();
  await assert.rejects(rpc.startWorkers([{...worker, calls: [
    {...worker.calls[0], unexpected: true}]}]), /worker call/);
  assert.equal(socket.writes.length, 1);
  const [owned] = await rpc.startWorkers([worker]);
  assert.equal(owned.index, 1);
  assert.equal(owned.nativeCalls, 2);
  assert.deepEqual(owned.logicalResultIndices, [1]);
  assert.equal(socket.writes[1].subarray(64).readUInt32LE(0), 1);
  assert.equal(socket.writes[1].subarray(64).readUInt8(8 + 48 + 9), 1);
  assert.deepEqual(await rpc.joinWorkers([{index: owned.index}]), [{results: ['55'], exitValue: '44'}]);
  await assert.rejects(rpc.joinWorkers([owned]), /not owned/);
  assert.equal(socket.writes.length, 3);
});

for (const variant of [{badIndex: true}, {truncated: true}])
  test('invalid worker metadata latches before a join: ' + JSON.stringify(variant), async () => {
    const {socket, rpc} = fixture((body, stream) => stream.emit('data', reply(startedPayload(variant), 1n)));
    await rpc.status();
    await assert.rejects(rpc.startWorkers([worker]));
    assert.ok(rpc.guard.error?.nativeRequestUncertain);
    await assert.rejects(rpc.joinWorkers([{index: 1}]));
    assert.equal(socket.writes.length, 2);
    assert.equal(socket.paused, true);
  });
