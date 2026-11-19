// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Retire only after the same-owner HEN, resident and game-arm checks pass.
// The pinned probe deliberately leaves application handles and scratch for
// Seedsow's normal exit: closing them from the live RPC froze earlier boots.
const fs = require('node:fs/promises');
const path = require('node:path');
const check = (ok, message) => { if (!ok) throw Error(message); };

function ready(report, acquisition) {
  check(report?.session === 'seedsow' && report.memoryControl?.ready === true &&
    report.memoryControl.socketPath === acquisition.socketPath &&
    !report.memoryControl.pending && !report.placementUncertain &&
    report.retainedMemory?.promoted === true &&
    report.retainedMemory.ready === true && report.retainedMemory.canonical === true &&
    report.retainedMemory.zeroCountVerified === true &&
    !report.retainedMemory.pending &&
    Array.isArray(report.retained?.threads) && report.retained.threads.length === 0 &&
    report.nativeRo?.worker?.joined === true && !report.error && !report.cleanupError,
  'Held Seedsow acquisition is not qualified for a unique success retirement');
}
function finished(report) {
  check(report?.session === 'seedsow' && report.completed === true &&
    typeof report.finished === 'string' && !report.error && !report.cleanupError &&
    !report.cacheRestoreError && !report.placementUncertain &&
    report.memoryControl?.ready === false && report.retainedMemory?.retired === true &&
    report.retainedMemory.ready === false && report.retained?.cacheNeedsRestore === false &&
    Array.isArray(report.retained.threads) && report.retained.threads.length === 0 &&
    (report.cleanedUp === true || report.cleanupSkipped === true),
  'Seedsow final report does not prove retirement and cache restoration');
  return {completed: true, cacheRestored: true, endpointClosed: true,
    nativeCleanupDeferredToAppExit: report.cleanupSkipped === true,
    cleanedUp: report.cleanedUp === true, finished: report.finished};
}
async function retire({acquisition, endpoint, producer, guard, run, listener}) {
  check(guard.error === null && endpoint.socketPath === acquisition.socketPath &&
    producer.owner.probeSettled === false &&
    typeof acquisition.report === 'string' && acquisition.report.startsWith(run + path.sep),
  'Seedsow success retirement lost its unique held owner');
  const report = JSON.parse(await fs.readFile(acquisition.report, 'utf8'));
  ready(report, acquisition);
  // One request only. Any lost reply or unfinished probe latches the shared
  // guard; never issue a second retire, stop, or native cleanup on this boot.
  const acknowledged = await endpoint.retireOnce(120000);
  let timer;
  try {
    await Promise.race([producer.owner.probePromise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(Error('Seedsow probe retirement deadline; result uncertain')),
        120000);
    })]);
  } finally { clearTimeout(timer); }
  check(producer.owner.probeSettled && !guard.error,
    'Seedsow probe did not settle after acknowledged retirement');
  const final = JSON.parse(await fs.readFile(acquisition.report, 'utf8'));
  const proof = finished(final);
  const stopped = await listener.stopAfterProvenSuccess();
  check(stopped.stopped && stopped.tcpClosed && stopped.serveEnded && !guard.error,
    'Seedsow listener closure was not proven');
  return {schema: 1, run, at: new Date().toISOString(), report: acquisition.report,
    endpoint: acknowledged, probe: proof, listener: stopped, hostOwnerExiting: true};
}
module.exports = {ready, finished, retire};
