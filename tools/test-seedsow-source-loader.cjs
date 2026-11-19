// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const assert = require('node:assert/strict');
const {test} = require('node:test');
const fs = require('node:fs');
const crypto = require('node:crypto');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const http = require('node:http');
const vm = require('node:vm');
const {createGuard} = require('./seedsow-transport.cjs');
const source = require('./seedsow-source-loader.cjs');
const root = path.resolve(__dirname, '..');
const provisioned = path.join(root, 'research/sources/seedsow-fs-kaihen');

test('held producer rejects changes to the bounded strong-only probe settings', () => {
  const {checkEnvironment} = require('./seedsow-held-producer.cjs');
  const allowed = {NS2_HEN: '0', NS2_SKIP_TEST_GATE: '1', NS2_MAIN_SWAP: '1',
    NS2_MAIN_SWAP_MODE: 'capture', NS2_POST_START_OPEN: '0', NS2_OPEN_SWAP: '0',
    NS2_CODE_SWAP: '0', NS2_FS_SWAP: '0', NS2_GROOM_ROUNDS: '4',
    NS2_ALIAS_BUCKETS: '5', NS2_ALIAS_OFFSET: '0x13b0000',
    NS2_FAST: '1', NS2_WAVE_SETTLE_MS: '1000', NS2_POSITIVE_ATTEMPTS: '1',
    NS2_SKIP_REPRO: '1',
    NS2_WEAK_ATTEMPT: '0', NS2_STABLE_ATTEMPT: '0'};
  assert.doesNotThrow(() => checkEnvironment(allowed));
  for (const [key, value] of [['NS2_GROOM_ROUNDS', '5'],
    ['NS2_ALIAS_BUCKETS', '3'],
    ['NS2_ALIAS_OFFSET', '0x14a0000'], ['NS2_FAST', '0'],
    ['NS2_WAVE_SETTLE_MS', '0'], ['NS2_POSITIVE_ATTEMPTS', '8'],
    ['NS2_SKIP_REPRO', '0'],
    ['NS2_WEAK_ATTEMPT', '1'], ['NS2_STABLE_ATTEMPT', '1']])
    assert.throws(() => checkEnvironment({...allowed, [key]: value}),
      /qualified bounded probe settings/);
});

test('actual pinned Seedsow consumer graph imports with one injected stop latch and no native IO',
  {skip: !fs.existsSync(provisioned) && 'Pinned source not provisioned'}, async () => {
    const run = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'switch-')));
    let native = 0;
    const deny = () => { ++native; throw Error('Source import must not open a native connection'); };
    net.createConnection = deny; http.request = deny;
    const guard = createGuard();
    const rpc = {a: {platform: 2, session: 'seedsow', sdk: 0x1000, main: 0x2000, scratch: 0x3000}, guard};
    const endpoint = {guard, socketPath: '/synthetic/owned.sock', bind: deny,
      request: deny, read: deny};
    try {
      const materialized = source.materializeHeld({root, run});
      assert.equal(Object.keys(materialized.files).length, 130);
      const fixture = source.TEST_FUNCTION;
      const fixtureFile = path.join(run, fixture.target);
      const fixtureBytes = fs.readFileSync(fixtureFile);
      assert.equal(fixtureBytes.length, fixture.bytes);
      assert.equal(crypto.createHash('sha256').update(fixtureBytes).digest('hex'), fixture.sha256);
      assert.deepEqual(materialized.derived[fixture.target], {...fixture});
      fs.chmodSync(fixtureFile, 0o600);
      fs.writeFileSync(fixtureFile, Buffer.alloc(fixture.bytes));
      assert.throws(() => source.create({root, run, rpc, endpoint, heldProducer: true,
        stages: {run() {}}, acquire: {start() {}}}),
      /derived test-function changed before import/);
      fs.writeFileSync(fixtureFile, fixtureBytes);
      fs.chmodSync(fixtureFile, 0o400);
      const workerFile = path.join(run, 'switch2/ro/worker-control.cjs');
      const workerSource = fs.readFileSync(workerFile, 'utf8');
      const normalizer = /const response = (bytes => \{[\s\S]*?\n\});/.exec(workerSource);
      assert.ok(normalizer, 'Materialized RO worker must normalize PID descriptors');
      const descriptor = 0x20000000000001n;
      const parse = vm.runInNewContext('(' + normalizer[1] + ')',
        {parseResponse: () => ({pid: descriptor, result: 0})});
      const normalized = parse(Buffer.alloc(0));
      assert.equal(normalized.pid, descriptor.toString());
      assert.doesNotThrow(() => JSON.stringify(normalized));
      const joinGate = /if \(command === 'worker-state' \|\| command === 'worker-join'\) \{[\s\S]*?if \(command === 'worker-settle'\)/
        .exec(workerSource)?.[0];
      assert.ok(joinGate, 'Materialized RO worker must keep its owned join path');
      assert.match(joinGate, /values\.every\(value => value !== workerCode\.SENTINEL\.toString\(\)\)/);
      assert.doesNotMatch(joinGate, /tryWait\.offset/);
      const gateBody = joinGate.slice(0, joinGate.lastIndexOf("  if (command === 'worker-settle')"));
      const sentinel = 0xffffffffffffffffn;
      let resultCell = sentinel, joins = 0, finalized = 0;
      const gateWorker = {base: 0x1000, resultOffsets: [0], logicalResultIndices: [0],
        kind: 'tcp-test', gate: 0x2000, results: []};
      const gateSession = {readOwned: async () => {
        const bytes = Buffer.alloc(8); bytes.writeBigUInt64LE(resultCell); return bytes;
      }, rpc: {call3: () => { throw Error('Unsafe native try-wait was called'); }}};
      const checkGate = vm.runInNewContext('(async command => {' + gateBody + '})', {
        s: gateSession, worker: gateWorker, workerCode: {SENTINEL: sentinel},
        check: (ok, message) => { if (!ok) throw Error(message); },
        native: {join: async () => { ++joins; gateWorker.results = [resultCell]; }},
        events: {finalize: async () => { ++finalized; }},
        save: async () => {},
      });
      assert.equal((await checkGate('worker-state')).finished, false);
      await assert.rejects(checkGate('worker-join'), /still pending/);
      assert.equal(joins, 0);
      resultCell = 0n;
      assert.equal((await checkGate('worker-state')).finished, true);
      assert.equal((await checkGate('worker-join')).finished, true);
      assert.equal(joins, 1);
      assert.equal(finalized, 1);
      const probeSource = fs.readFileSync(path.join(run,
        'switch2/memory/probe-reserved-gap-seedsow.cjs'), 'utf8');
      const earlyGate = /range\.fragmentationSkipped = range\.earlyLinkCandidates\.some\(item =>[\s\S]*?\)\);/
        .exec(probeSource);
      assert.ok(earlyGate, 'Materialized probe must preserve the early link gate');
      const gateContext = {range: {earlyLinkCandidates: [{encodable: true, zeroAfter32: true}]},
        process: {env: {NS2_WEAK_ATTEMPT: '0'}}};
      vm.runInNewContext(earlyGate[0], gateContext);
      assert.equal(gateContext.range.fragmentationSkipped, false);
      gateContext.range.earlyLinkCandidates = [{encodable: true, changedLink: true}];
      vm.runInNewContext(earlyGate[0], gateContext);
      assert.equal(gateContext.range.fragmentationSkipped, true);
      const loader = source.create({root, run, rpc, endpoint});
      for (const [name, exportName] of [
        ['switch2/app/session.cjs', 'Session'],
        ['switch2/tools/fs-memory.cjs', 'connect'],
        ['switch2/ro/runtime.cjs', 'handoff'],
        ['switch2/ro/package.cjs', 'dispatch'],
      ]) assert.equal(typeof loader.load(name)[exportName], 'function');
      assert.equal(loader.load('shared/rpc.cjs'), rpc);
      assert.throws(() => loader.load('tools/console-rpc.cjs'), /outside the pinned/);
      assert.throws(() => loader.load('../shared/rpc.cjs'), /outside the pinned/);
      assert.equal(loader.sourceSha256, '4d4375f2729ca4b95a98348419c1e2b7edffa684bf53316f00a38194c5f4ca7b');
      const producer = require('./seedsow-held-producer.cjs').create({run, guard, endpoint});
      const held = source.create({root, run, rpc, endpoint, heldProducer: true,
        stages: producer.stages, acquire: producer.acquire});
      producer.attach(held);
      assert.equal(held.load('shared/stages.cjs'), producer.stages);
      assert.equal(held.load('switch2/acquire.cjs'), producer.acquire);
      assert.equal(typeof held.load('switch2/pipeline.cjs').main, 'function');
      assert.equal(typeof held.load('switch2/memory/probe-reserved-gap-seedsow.cjs').moaderMain, 'function');
      assert.equal(typeof held.load('switch2/process/qualify.cjs').main, 'function');
      const qualificationSource = fs.readFileSync(path.join(run,
        'switch2/process/qualify.cjs'), 'utf8');
      const identityGate = /check\(!proof\.app\.result && ([^\n]+),\n\s+'The transferred current-process handle is not Seedsow Lullaby Demo or full game'\);/
        .exec(qualificationSource);
      assert.ok(identityGate, 'The materialized process gate must retain its result and title checks');
      const qualifies = (programId, result = 0) => vm.runInNewContext(
        'Boolean(!proof.app.result && ' + identityGate[1] + ')',
        {proof: {app: {programId, result}}});
      assert.equal(qualifies('010042d022aea000'), true);
      assert.equal(qualifies('0100ee5021c9e000'), true);
      assert.equal(qualifies('0100c9100b06a000'), false);
      assert.equal(qualifies('0100ee5021c9e000', 1), false);
      assert.equal(typeof held.load('switch2/app/inspect-mounts.cjs').moaderMain, 'function');
      for (const relative of require('./seedsow-source-transforms.cjs').STAGE_ENTRYPOINTS)
        assert.equal(typeof held.load(relative).moaderMain, 'function', relative);
      // The diagnostic launch caches this module before the HEN launch swaps
      // gateModule.Gate. Its constructor must therefore read the live export.
      const launchSource = fs.readFileSync(path.join(run, 'switch2/ro/launch.cjs'), 'utf8');
      assert.match(launchSource, /const gateModule = require\("\.\/gate\.cjs"\);/);
      assert.match(launchSource, /const gate = new gateModule\.Gate\(memory, row,/);
      assert.doesNotMatch(launchSource, /const \{Gate\} = require\("\.\/gate\.cjs"\);/);
      assert.equal(typeof held.load('switch2/ro/launch.cjs').main, 'function');
      const mounts = {session: 'seedsow', scope: 'save-only', sdk: rpc.a.sdk,
        mounts: [{name: 'save', accessor: 0x100000100,
        implementation: 0x100000200, service: 0x100000300,
        serviceVtable: 0x100000400, serviceHex: '00'.repeat(128),
        serviceVtableHex: '00'.repeat(160)}]};
      const mountFile = path.join(run, 'switch2/app/mounts.json');
      const mountBytes = JSON.stringify(mounts);
      fs.writeFileSync(mountFile, mountBytes);
      const mountProof = held.qualifyMounts();
      assert.equal(mountProof.sdk, rpc.a.sdk);
      fs.writeFileSync(mountFile, mountBytes + '\n');
      assert.throws(() => held.load('switch2/app/mounts.json'), /changed before import/);
      fs.writeFileSync(mountFile, mountBytes);
      assert.deepEqual(held.load('switch2/app/mounts.json'), mounts);
      assert.throws(() => held.qualifyMounts(), /once per fresh run/);
      assert.equal(Object.keys(held.materialized()).length, Object.keys(source.TRANSFORMED).length);
      const pinning = held.load('switch2/memory/pinning-control.cjs');
      let measuredPin = null, attempts = 0, released = 0;
      const missed = {profile: 'abort-control', pinningCreated: true,
        restored: true, newPersistentBytes: 7440, triggerAborted: true,
        nativeGuard: {completed: 1}, changedPaths: [], canaryResult: 0,
        canaryType: 1, pinningOverlapped: false};
      const retryResult = await pinning.retryPinningControl(async () => {
        if (++attempts === 1) {
          measuredPin = 7440;
          throw new pinning.PinningWindowMiss(missed);
        }
        return {restored: true};
      }, async (row, attempt) => {
        assert.equal(row, missed); assert.equal(attempt, 1); ++released;
      }, () => measuredPin);
      assert.deepEqual({retryResult, attempts, released},
        {retryResult: {restored: true}, attempts: 2, released: 1});
      const {unusedTail} = held.load('switch2/retained/cache-shape.cjs');
      const pathFilledArray = Buffer.alloc(0x5a0);
      pathFilledArray.fill(0x42, 0x30);
      const positiveProof = {canaryChanged: true, markerSelected: true,
        cases: [{profile: 'positive', allPending: true, canaryType: 0,
          changedPaths: ['canary']}]};
      assert.equal(unusedTail(pathFilledArray), false);
      assert.equal(unusedTail(pathFilledArray, {...positiveProof, markerSelected: false}), false);
      assert.equal(unusedTail(pathFilledArray, positiveProof), true);
      pathFilledArray[0x100] = 0x43;
      assert.equal(unusedTail(pathFilledArray, positiveProof), false);
      assert.equal(native, 0);
      assert.equal(guard.error, null);
    } finally { fs.rmSync(run, {recursive: true, force: true}); }
  });
