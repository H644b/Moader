// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const assert = require('node:assert/strict');
const {test} = require('node:test');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const crypto = require('node:crypto');
const {createGuard} = require('./seedsow-transport.cjs');
const {create, ALLOWED} = require('./seedsow-endpoint.cjs');

test('retained command coverage follows the pinned FS proof path without cleanup commands', async () => {
  const root = path.join(__dirname, '../research/sources/seedsow-fs-kaihen/ns2jb-main/ns2jb-main/engine/switch2');
  const files = ['pipeline.cjs', ...['ro', 'process', 'retained', 'tools'].flatMap(folder =>
    require('node:fs').readdirSync(path.join(root, folder)).filter(name => name.endsWith('.cjs'))
      .map(name => path.join(folder, name)))];
  const exclusions = new Set(['retire', 'root-native-donor-cleanup', 'rpc-shutdown', 'say', 'status']);
  for (const relative of files) {
    const source = await fs.readFile(path.join(root, relative), 'utf8');
    for (const [, command] of source.matchAll(/command\s*:\s*['"]([^'"]+)['"]/g)) {
      if (command === 'root-native-') continue; // prepare-event constructs its suffix from a fixed local array.
      if (exclusions.has(command)) continue;
      assert.ok(ALLOWED.has(command), `${relative} requires ${command}`);
    }
  }
  for (const command of exclusions) assert.ok(!ALLOWED.has(command), `${command} must not enter the endpoint`);
});

async function fixture(handler, action, guard = createGuard()) {
  const run = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'moader-seedsow-endpoint-')));
  const socketPath = path.join(run, 'memory.sock'), sockets = new Set();
  const server = net.createServer(socket => {
    sockets.add(socket); socket.on('close', () => sockets.delete(socket));
    let input = '';
    socket.on('data', part => {
      input += part;
      if (input.includes('\n')) handler(JSON.parse(input.slice(0, input.indexOf('\n'))), socket, run);
    });
  });
  try {
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(socketPath, resolve); });
    return await action({endpoint: create({socketPath, guard}), run, socketPath, sockets});
  } finally {
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => server.close(resolve));
    await fs.rm(run, {recursive: true, force: true});
  }
}

test('same-owner endpoint can await a console operation under the shared latch', () => {
  const guard = createGuard();
  return fixture((request, socket) => {
    assert.equal(request.command, 'root-native-info');
    guard.run(async () => ({ready: true})).then(result =>
      socket.end(JSON.stringify(result) + '\n'), error => socket.destroy(error));
  }, async ({endpoint}) => {
    assert.deepEqual(await endpoint.request({command: 'root-native-info'}, 500), {ready: true});
    assert.equal(guard.error, null);
  }, guard);
});

test('fixed macOS temp alias is accepted while nested symlinks remain rejected', () =>
  fixture((request, socket) => {
    assert.equal(request.command, 'root-native-info');
    socket.end('{"ready":true}\n');
  }, async ({run, socketPath}) => {
    if (process.platform !== 'darwin') return;
    assert.ok(socketPath.startsWith('/private/var/folders/'));
    const alias = socketPath.slice('/private'.length);
    const owned = create({socketPath: alias, guard: createGuard()});
    assert.deepEqual(await owned.request({command: 'root-native-info'}), {ready: true});
    await fs.symlink(run, path.join(run, 'linked'));
    assert.throws(() => create({socketPath: path.join(run, 'linked/memory.sock'),
      guard: createGuard()}), /Symlink in Seedsow endpoint path/);
  }));

test('one retained request uses its exact owned socket and rejects retirement', () => fixture((request, socket) => {
  assert.deepEqual(request, {command: 'root-native-info'});
  socket.end(JSON.stringify({ready: true}) + '\n');
}, async ({endpoint}) => {
  assert.deepEqual(await endpoint.request({command: 'root-native-info'}), {ready: true});
  await assert.rejects(endpoint.request({command: 'retire'}), /Unsupported Seedsow retained command/);
  assert.ok(endpoint.guard.error?.nativeRequestUncertain);
}));

test('pinned native worker bind and cancel arrays keep the same owner transport', () => {
  const workers = Array.from({length: 7}, (_, i) => ({
    address: 0x100000 + i * 0x100, sourceHandle: i + 1, id: 70 + i,
    entry: 0x200000, handle: 0x300000 + i,
  }));
  const cancellation = workers.map(row => ({handle: row.handle, result: 0}));
  let count = 0;
  return fixture((request, socket) => {
    ++count;
    if (request.command === 'root-native-worker-bind') {
      assert.deepEqual(request.workers, workers);
      socket.end(JSON.stringify(workers) + '\n');
    } else if (request.command === 'root-native-worker-cancel') {
      socket.end(JSON.stringify(cancellation) + '\n');
    } else socket.end('{"ready":true}\n');
  }, async ({endpoint}) => {
    assert.deepEqual(await endpoint.request({command: 'root-native-worker-bind', workers}), workers);
    assert.deepEqual(await endpoint.request({command: 'root-native-worker-cancel'}), cancellation);
    assert.deepEqual(await endpoint.request({command: 'root-native-info'}), {ready: true});
    assert.equal(count, 3);
    assert.equal(endpoint.guard.error, null);
  });
});

test('unexpected worker array latches without sending another native request', () => {
  let count = 0;
  return fixture((_request, socket) => {
    ++count;
    socket.end('[{"result":0}]\n');
  }, async ({endpoint}) => {
    await assert.rejects(endpoint.request({command: 'root-native-info'}),
      /Malformed Seedsow native worker array/);
    await assert.rejects(endpoint.request({command: 'root-native-worker-cancel'}));
    assert.equal(count, 1);
    assert.ok(endpoint.guard.error?.nativeRequestUncertain);
  });
});

test('lost reply after dispatch latches, preserves the owned client and never retries', () => {
  let count = 0;
  return fixture((request, socket) => { ++count; assert.equal(request.command, 'root-native-file-stage');
    // Simulate native completion followed by a lost response.
    socket.end();
  }, async ({endpoint}) => {
    const tasks = [endpoint.request({command: 'root-native-file-stage', target: '/x'}, 100),
      endpoint.request({command: 'root-native-close-handle', handle: 1})];
    const results = await Promise.allSettled(tasks);
    assert.equal(count, 1);
    assert.ok(results.every(row => row.status === 'rejected' && row.reason === endpoint.guard.error));
    assert.ok(endpoint.guard.error?.nativeRequestUncertain);
  });
});

test('malformed, duplicate and explicit error replies stop queued native requests', async () => {
  for (const response of ['{}\n{}\n', '{', '{"error":"worker still active"}\n']) {
    let count = 0;
    await fixture((request, socket) => { ++count; socket.end(response); }, async ({endpoint}) => {
      const results = await Promise.allSettled([
        endpoint.request({command: 'root-native-info'}),
        endpoint.request({command: 'root-native-close-handle', handle: 1}),
      ]);
      assert.equal(count, 1);
      assert.ok(results.every(row => row.status === 'rejected'));
      assert.ok(endpoint.guard.error?.nativeRequestUncertain);
    });
  }
});

test('retained file read validates scoped canonical path, metadata and full digest', () => fixture(async (request, socket, run) => {
  assert.equal(request.command, 'read');
  const bytes = Buffer.from('current-run-data');
  const outputFile = path.join(run, 'capture.bin');
  await fs.writeFile(outputFile, bytes);
  socket.end(JSON.stringify({address: request.address, length: request.length, outputFile,
    sha256: crypto.createHash('sha256').update(bytes).digest('hex')}) + '\n');
}, async ({endpoint, run}) => {
  const result = await endpoint.read(0x100000, 16, run);
  assert.equal(result.bytes.toString(), 'current-run-data');
  assert.equal(endpoint.guard.error, null);
}));

test('output path outside the fresh run latches before acceptance', () => fixture((request, socket) => {
  socket.end(JSON.stringify({address: request.address, length: request.length,
    outputFile: '/tmp/foreign-capture.bin', sha256: '0'.repeat(64)}) + '\n');
}, async ({endpoint, run}) => {
  await assert.rejects(endpoint.read(0x100000, 4, run), /escaped its run/);
  assert.ok(endpoint.guard.error?.nativeRequestUncertain);
}));
