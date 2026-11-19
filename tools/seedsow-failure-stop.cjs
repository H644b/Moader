// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Admission for a single, definite stage-2 miss. This does not close native
// handles or remove the save scratch: upstream's completed native teardown has
// frozen this console. It only permits the original owned RPC loop to stop,
// after the probe proves there is no pending worker or cache restoration.
const fs = require('node:fs');
const path = require('node:path');

function classify(run, {probeSettled, guardError}) {
  const reject = reason => ({eligible: false, reason});
  if (!probeSettled || guardError) return reject('producer or transport is still uncertain');
  try {
    if (fs.realpathSync(run) !== run || !/^switch-[A-Za-z0-9]+$/.test(path.basename(run)))
      return reject('run identity differs');
    const pipeline = JSON.parse(fs.readFileSync(path.join(run, 'pipeline.json')));
    const stages = pipeline.stages || [];
    if (stages.at(-1)?.name !== 'FS read, write and restoration' ||
        stages.at(-1)?.status !== 'failed' ||
        stages.some(row => row.status === 'running'))
      return reject('failure was not a settled stage-2 miss');
    const directory = path.join(run, 'switch2/memory');
    const files = fs.readdirSync(directory).filter(name => /^reserved-gap-[0-9]+\.json$/.test(name));
    if (files.length !== 1) return reject('acquisition report is not unique');
    const reportFile = path.join(directory, files[0]);
    const report = JSON.parse(fs.readFileSync(reportFile));
    const retained = report.retained || {};
    const stableOnly = String(report.error || '').startsWith(
      'Error: No changedLink/changedHeader/zeroTail candidate on this boot (');
    const positive = report.firstPositiveExhaustion;
    const positiveRows = report.cases?.filter(row => row.profile === 'positive');
    const positiveMiss = String(report.error || '').startsWith(
      'Error: First-positive waves exhausted after 1 attempts without strict 0/type0 canary selection; owned cleanup allowed after every wave passed its checks') &&
      positive?.attempts === 1 && positive.allowed === 1 &&
      positive.continuations === 0 && positive.terminalAborted === true &&
      positive.cleanupEligible === true && Array.isArray(positive.uncertainWaves) &&
      positive.uncertainWaves.length === 0 && Array.isArray(positiveRows) &&
      positiveRows.length === 1 && positiveRows[0].triggerAborted === true &&
      positiveRows[0].restored === true && positiveRows[0].canaryResult === 0 &&
      positiveRows[0].canaryType === 1 && positiveRows[0].diskHealthy === true &&
      Array.isArray(positiveRows[0].changedPaths) && positiveRows[0].changedPaths.length === 0;
    const alias = report.cacheAlias;
    const aliasMiss = String(report.error || '').startsWith(
      'Error: Combined acquisition did not obtain the clean alias') &&
      report.canaryChanged === true && report.markerSelected === true &&
      report.markerReproduced === true && alias?.clean === true &&
      alias.chosenBucket === null && alias.restored === true &&
      Array.isArray(alias.bucketOrder) && alias.bucketOrder.length > 0 &&
      alias.bucketOrder.length <= 5 && Array.isArray(alias.attempts) &&
      alias.attempts.length === alias.bucketOrder.length &&
      alias.attempts.every((row, index) => row.bucket === index &&
        row.result === 0 && row.selectedAlias === false) &&
      Array.isArray(alias.restoreChecks) &&
      alias.restoreChecks.length === alias.attempts.length &&
      alias.restoreChecks.every(row => row.result === 0 && row.matches === true);
    if (report.session !== 'seedsow' || !report.finished ||
        !(stableOnly || aliasMiss || positiveMiss) ||
        report.placementUncertain || report.nativeRequestUncertain ||
        report.firstPositiveGuardPending || report.cacheRestoreError || report.cleanupError ||
        report.memoryControl || report.retainedMemory || report.nativeRo ||
        retained.cacheNeedsRestore !== false ||
        !Array.isArray(retained.threads) || retained.threads.length !== 0 ||
        !Array.isArray(retained.workers) || !retained.workers.length ||
        retained.workers.some(worker => worker.joined !== true) ||
        retained.fileHandle || retained.group || retained.originalRom ||
        report.control?.mode !== 'closed' || report.control?.active !== false ||
        !(report.cleanupSkipped === true || report.cleanedUp === true))
      return reject('probe did not prove a closed, restorable stage-2 miss');
    return {eligible: true, reportFile, scratch: report.scratch,
      retainedHandles: Array.isArray(retained.handles) ? retained.handles.length : null,
      cacheRestored: true, workersJoined: retained.workers.length,
      nativeCleanup: report.cleanedUp === true ? 'complete' : 'deferred-to-app-exit-or-reboot'};
  } catch (error) { return reject('missing or unreadable failure evidence: ' + error.message); }
}

module.exports = {classify};
