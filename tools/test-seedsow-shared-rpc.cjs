// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const assert = require('node:assert/strict');
const {test} = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {createGuard} = require('./seedsow-transport.cjs');
const {create} = require('./seedsow-shared-rpc.cjs');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
async function fixture(action) {
  const run = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'moader-seedsow-shared-')));
  try {
    const connectionFile = path.join(run, 'connection.json'), bytes = Buffer.from('{"session":"seedsow"}\n');
    fs.writeFileSync(connectionFile, bytes);
    const guard = createGuard(), calls = [];
    const rpc = {guard, binding: {platform: 2, sdk: 0x1000, main: 0x2000, scratch: 0x3000,
      session: 'seedsow'}, output: 0x7040,
    async status() { calls.push('status'); return this.binding; },
    async call(offset, args) { calls.push(['call', offset, args]); return {result: 7n, output: Buffer.alloc(64, 0x55)}; },
    async callAddress(address, args) { calls.push(['call-address', address, args]); return {result: 8n, output: Buffer.alloc(64)}; },
    async read(address, length) { calls.push(['read', address, length]); return Buffer.alloc(length, 0x3a); },
    async write(address, data) { calls.push(['write', address, data.toString('hex')]); },
    async query(address) { calls.push(['query', address]); return {address, size: 0x1000}; },
    async verifyFunction(address, data) { calls.push(['verify', address, data]); return {verified: true}; },
    async startWorkers(workers) { calls.push(['start', workers]); return [{index: 1}]; },
    async joinWorkers(workers) { calls.push(['join', workers]); return [{results: ['0'], exitValue: '0'}]; },
    async say(text) { calls.push(['say', text]); return {said: true}; }};
    const bridge = create({rpc, connectionFile, connectionSha256: hash(bytes), run});
    return await action({bridge, rpc, guard, calls, connectionFile, run});
  } finally { fs.rmSync(run, {recursive: true, force: true}); }
}

test('qualified source adapter maps the reviewed bridge API without connecting', async () => {
  await fixture(async ({bridge, calls, run}) => {
    assert.deepEqual(await bridge.request({command: 'status'}), {...bridge.a, ready: true});
    assert.deepEqual(await bridge.request({command: 'call', offset: 9, args: [1]}),
      {result: '7', output: Buffer.alloc(64, 0x55).toString('base64')});
    assert.equal((await bridge.request({command: 'read', address: 0x5000, length: 3})).data, 'Ojo6');
    assert.deepEqual(await bridge.request({command: 'write', address: 0x5000, data: 'AQID'}),
      {written: true});
    assert.deepEqual(await bridge.request({command: 'workers-start', workers: [{}]}),
      {workers: [{index: 1}]});
    assert.deepEqual(await bridge.request({command: 'workers-join', workers: [{index: 1}]}),
      {workers: [{results: ['0'], exitValue: '0'}]});
    assert.equal(bridge.directory, path.join(run, 'switch2'));
    assert.equal(bridge.output, 0x7040);
    assert.equal(typeof bridge.pair, 'function');
    assert.equal(calls.length, 6);
  });
});

test('pair validates both calls before the first native dispatch', async () => {
  await fixture(async ({bridge, calls, guard}) => {
    await assert.rejects(bridge.request({command: 'pair', calls: [
      {offset: 1, args: []}, {offset: 2, args: [1, 2, 3, 4, 5]},
    ]}), /paired call/);
    assert.deepEqual(calls, []);
    assert.equal(guard.error, null);
    assert.deepEqual(await bridge.request({command: 'pair', calls: [
      {offset: 1, args: []}, {offset: 2, args: [3]},
    ]}), {results: ['7', '7'], output: Buffer.alloc(64, 0x55).toString('base64')});
    assert.equal(calls.length, 2);
  });
});

test('the source application batch uses the direct paired-call ABI once per item', async () => {
  await fixture(async ({bridge, calls}) => {
    assert.deepEqual(await bridge.pair([{offset: 1, args: [2]}, {offset: 3, args: [4]}]),
      {results: [7n, 7n], output: Buffer.alloc(64, 0x55)});
    assert.deepEqual(calls, [['call', 1n, [2n]], ['call', 3n, [4n]]]);
  });
});

test('changed connection inode or bytes latches before another native dispatch', async () => {
  await fixture(async ({bridge, calls, guard, connectionFile}) => {
    await bridge.status();
    fs.writeFileSync(connectionFile, '{"session":"other"}\n');
    await assert.rejects(bridge.call(1, []), /connection changed/);
    assert.ok(guard.error?.nativeRequestUncertain);
    assert.deepEqual(calls, ['status']);
  });
});

test('script and shutdown bridge commands are unavailable', async () => {
  await fixture(async ({bridge, calls, guard}) => {
    await assert.rejects(bridge.request({command: 'rpc-run', source: 'print(1)'}), /Unsupported/);
    assert.ok(guard.error?.nativeRequestUncertain);
    assert.deepEqual(calls, []);
  });
});
