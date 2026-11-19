// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const assert = require('node:assert/strict');
const {test} = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {classify} = require('./seedsow-failure-stop.cjs');

function fixture(action) {
  const base = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'seedsow-stop-test-')));
  const run = path.join(base, 'switch-fixture');
  const directory = path.join(run, 'switch2/memory');
  fs.mkdirSync(directory, {recursive: true});
  const pipeline = {stages: [
    {name: 'app/map', status: 'complete'},
    {name: 'FS read, write and restoration', status: 'failed'},
  ]};
  const report = {session: 'seedsow', finished: '2026-11-18T00:00:00.000Z',
    error: 'Error: No changedLink/changedHeader/zeroTail candidate on this boot (36 stableLink; NS2_STABLE_ATTEMPT=0)',
    cleanupSkipped: true, cleanedUp: false, scratch: '/NS2_GAP_fixture',
    control: {mode: 'closed', active: false}, retained: {
      threads: [], workers: [{joined: true}], cacheNeedsRestore: false,
      fileHandle: null, group: null, originalRom: null, handles: [3, 4],
    }};
  const write = () => {
    fs.writeFileSync(path.join(run, 'pipeline.json'), JSON.stringify(pipeline));
    fs.writeFileSync(path.join(directory, 'reserved-gap-1.json'), JSON.stringify(report));
  };
  try { write(); return action({run, report, pipeline, write, directory}); }
  finally { fs.rmSync(base, {recursive: true, force: true}); }
}

test('a settled stable-only miss admits only listener retirement', () => fixture(({run}) => {
  const result = classify(run, {probeSettled: true, guardError: null});
  assert.equal(result.eligible, true);
  assert.equal(result.nativeCleanup, 'deferred-to-app-exit-or-reboot');
  assert.equal(result.cacheRestored, true);
  assert.equal(result.retainedHandles, 2);
}));

test('a fully restored alias miss admits only listener retirement', () => fixture(({run, report, write}) => {
  report.error = 'Error: Combined acquisition did not obtain the clean alias';
  report.canaryChanged = report.markerSelected = report.markerReproduced = true;
  report.cacheAlias = {clean: true, chosenBucket: null, restored: true,
    bucketOrder: [0x14a0000, 0x13b0000],
    attempts: [{bucket: 0, result: 0, selectedAlias: false},
      {bucket: 1, result: 0, selectedAlias: false}],
    restoreChecks: [{result: 0, matches: true}, {result: 0, matches: true}]};
  write();
  const args = {probeSettled: true, guardError: null};
  assert.equal(classify(run, args).eligible, true);
  for (const mutate of [
    () => { report.cacheAlias.restoreChecks.pop(); },
    () => { report.cacheAlias.restoreChecks[0].matches = false; },
    () => { report.cacheAlias.attempts[1].selectedAlias = true; },
    () => { report.cacheAlias.chosenBucket = 1; },
    () => { report.retained.cacheNeedsRestore = true; },
  ]) {
    const before = structuredClone(report);
    mutate(); write();
    assert.equal(classify(run, args).eligible, false);
    Object.assign(report, before); write();
  }
}));

test('one source-qualified aborted positive miss admits only listener retirement', () => fixture(({run, report, write}) => {
  report.error = 'Error: First-positive waves exhausted after 1 attempts without strict 0/type0 canary selection; owned cleanup allowed after every wave passed its checks';
  report.firstPositiveExhaustion = {attempts: 1, allowed: 1, continuations: 0,
    terminalAborted: true, cleanupEligible: true, uncertainWaves: []};
  report.firstPositiveGuardPending = false;
  report.cases = [{profile: 'positive', triggerAborted: true, restored: true,
    canaryResult: 0, canaryType: 1, diskHealthy: true, changedPaths: []}];
  write();
  const args = {probeSettled: true, guardError: null};
  assert.equal(classify(run, args).eligible, true);
  for (const mutation of [
    () => { report.firstPositiveGuardPending = true; },
    () => { report.firstPositiveExhaustion.cleanupEligible = false; },
    () => { report.firstPositiveExhaustion.uncertainWaves.push({wave: 1}); },
    () => { report.firstPositiveExhaustion.attempts = 2; },
    () => { report.cases[0].triggerAborted = false; },
    () => { report.cases[0].canaryType = 0; },
    () => { report.cases[0].changedPaths.push('/unexpected'); },
    () => { report.retained.workers[0].joined = false; },
  ]) {
    const before = structuredClone(report);
    mutation(); write();
    assert.equal(classify(run, args).eligible, false);
    for (const key of Object.keys(report)) delete report[key];
    Object.assign(report, before); write();
  }
}));

test('a pending or ambiguous failure cannot dispatch a stop', () => fixture(({run, report, pipeline, write}) => {
  const args = {probeSettled: true, guardError: null};
  assert.equal(classify(run, {...args, probeSettled: false}).eligible, false);
  assert.equal(classify(run, {...args, guardError: Error('lost reply')}).eligible, false);
  for (const mutation of [
    () => { report.retained.cacheNeedsRestore = true; },
    () => { report.retained.threads.push(7); },
    () => { report.retained.workers[0].joined = false; },
    () => { report.memoryControl = {ready: true}; },
    () => { report.placementUncertain = true; },
    () => { report.cleanupError = 'cleanup timed out'; },
    () => { report.control.mode = 'retiring'; },
    () => { pipeline.stages.push({name: 'ro/install-retained', status: 'failed'}); },
  ]) {
    const previousReport = structuredClone(report), previousPipeline = structuredClone(pipeline);
    mutation(); write();
    assert.equal(classify(run, args).eligible, false);
    for (const key of Object.keys(report)) delete report[key];
    for (const key of Object.keys(pipeline)) delete pipeline[key];
    Object.assign(report, previousReport); Object.assign(pipeline, previousPipeline); write();
    assert.equal(classify(run, args).eligible, true);
  }
}));
