// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {qualifyHenInspection} = require('./seedsow-install.cjs');

function fixture() {
  const file = '/current/run/switch2/ro/hen-startup.json';
  return {
    receipt: {hen: {pid: '219', file}},
    inspection: {
      complete: true, controllerHandlesClosed: true, resourcesRetained: false,
      borrowedRoRetained: true, borrowedRo: {sessionId: 'seedsow', file},
      hen: {pid: '218', protocol: 7, result: 0},
      game: {phase: 3, result: 0},
      plugins: {names: ['moader'], enumerationComplete: true},
      moader: {listed: true, resident: true, serviceReachable: true,
        summary: {backendConnected: true, routingGenerationCurrent: true}},
    },
  };
}

test('a same-owner HEN and resident reply accepts the distinct plugin launch PID', () => {
  const {receipt, inspection} = fixture();
  assert.notEqual(inspection.hen.pid, receipt.hen.pid);
  assert.equal(qualifyHenInspection(inspection, receipt), true);
});

test('foreign lease, failed HEN, absent resident or stale routing fails closed', () => {
  const mutations = [
    x => { x.inspection.borrowedRo.file = '/other/run/hen-startup.json'; },
    x => { x.inspection.borrowedRoRetained = false; },
    x => { x.inspection.controllerHandlesClosed = false; },
    x => { x.inspection.hen.result = 1; },
    x => { x.inspection.hen.pid = '0'; },
    x => { x.inspection.plugins.names = []; },
    x => { x.inspection.moader.serviceReachable = false; },
    x => { x.inspection.moader.summary.backendConnected = false; },
    x => { x.inspection.moader.summary.routingGenerationCurrent = false; },
    x => { x.inspection.game.phase = 5; },
    x => { x.inspection.game.result = 1; },
  ];
  for (const mutate of mutations) {
    const sample = fixture(); mutate(sample);
    assert.throws(() => qualifyHenInspection(sample.inspection, sample.receipt),
      /same-owner status check/);
  }
});
