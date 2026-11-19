// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const {ready, finished, retire} = require('./seedsow-success-retire.cjs');

function held(socketPath) {
  return {session: 'seedsow', memoryControl: {ready: true, socketPath},
    retainedMemory: {promoted: true, ready: true, canonical: true, zeroCountVerified: true},
    retained: {threads: [], cacheNeedsRestore: true}, nativeRo: {worker: {joined: true}}};
}
function finalReport(source) {
  return {...source, completed: true, finished: new Date().toISOString(),
    memoryControl: {...source.memoryControl, ready: false, phase: 'retired'},
    retainedMemory: {...source.retainedMemory, ready: false, retired: true},
    retained: {...source.retained, cacheNeedsRestore: false}, cleanupSkipped: true,
    cleanedUp: false};
}
test('retirement waits for final cache proof and closes the owned listener once', async () => {
  const run = await fs.mkdtemp(path.join(os.tmpdir(), 'switch-'));
  try {
    const report = path.join(run, 'probe.json'), socketPath = path.join(run, 'memory.sock');
    await fs.writeFile(report, JSON.stringify(held(socketPath)));
    const acquisition = {report, socketPath};
    const guard = {error: null};
    let sends = 0, stops = 0, settled = false;
    const producer = {owner: {get probeSettled() { return settled; },
      probePromise: Promise.resolve()}};
    const endpoint = {socketPath, async retireOnce() {
      sends++;
      await fs.writeFile(report, JSON.stringify(finalReport(held(socketPath))));
      settled = true;
      return {retired: true};
    }};
    const listener = {async stopAfterProvenSuccess() {
      stops++;
      assert.equal(JSON.parse(await fs.readFile(report)).retained.cacheNeedsRestore, false);
      return {stopped: true, tcpClosed: true, serveEnded: true};
    }};
    const receipt = await retire({acquisition, endpoint, producer, guard, run, listener});
    assert.equal(sends, 1); assert.equal(stops, 1);
    assert.equal(receipt.probe.nativeCleanupDeferredToAppExit, true);
    assert.equal(receipt.listener.tcpClosed, true);
  } finally { await fs.rm(run, {recursive: true, force: true}); }
});
test('unretired or uncertain cache state cannot stop the listener', () => {
  const state = held('/tmp/owned.sock');
  assert.doesNotThrow(() => ready(state, {socketPath: '/tmp/owned.sock'}));
  assert.throws(() => finished(state), /final report/);
  assert.throws(() => finished({...finalReport(state), retained: {...state.retained,
    cacheNeedsRestore: true}}), /final report/);
  assert.throws(() => ready({...state, nativeRo: {worker: {joined: false}}},
    {socketPath: '/tmp/owned.sock'}), /not qualified/);
});
