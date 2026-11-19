// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Deterministic, locally materialized changes to the SHA-pinned Seedsow engine.
// The original 140-file source closure stays untouched. Every replacement is
// exact and fails closed if the pinned revision's control flow changes.
const crypto = require('node:crypto');
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
const check = (ok, message) => { if (!ok) throw Error(message); };
// Both editions use the reviewed Seedsow main/SDK code profile, but their
// application identities differ. Keep the current-process handle limited to
// these two known IDs; every other title remains a hard stop.
const SEEDSOW_IDS = Object.freeze({
  demo: '010042d022aea000',
  full: '0100ee5021c9e000',
});
const STAGE_ENTRYPOINTS = new Set([
  'switch2/app/map.cjs', 'switch2/ro/install-retained.cjs',
  'switch2/ro/test-sm.cjs', 'switch2/ro/copy-loader-root.cjs',
  'switch2/ro/prepare-workers.cjs', 'switch2/ro/bind-sessions.cjs',
  'switch2/ro/prepare-event.cjs',
]);

function once(source, old, replacement, label) {
  const first = source.indexOf(old);
  check(first >= 0 && source.indexOf(old, first + 1) < 0,
    'Pinned Seedsow ' + label + ' seam changed');
  return source.slice(0, first) + replacement + source.slice(first + old.length);
}
function transform(relative, original) {
  check(typeof relative === 'string' && typeof original === 'string',
    'Invalid Seedsow source transform input');
  let source = original;
  if (relative === 'switch2/pipeline.cjs') {
    // The Moader CLI has its own arguments. Only the reviewed, environment
    // selected non-HEN pipeline path may run in this private source graph.
    source = once(source, '  const flags = process.argv.slice(2);',
      '  const flags = [];', 'pipeline command-line isolation');
    source = once(source, "    await script('app', 'map');",
      "    await script('app', 'map');\n    await script('app', 'inspect-mounts');",
      'current-boot mount inspection');
    const from = source.indexOf('    const donorCleanup = await stage(\'Remove RO donor files\'');
    const to = source.indexOf('  } catch (error) {\n    record.error = error.stack;', from);
    check(from >= 0 && to > from && source.slice(from, to).includes('acquisition.retire()') &&
      source.slice(from, to).includes('record.verified = true; record.result = result;'),
    'Pinned Seedsow FS-proof handoff seam changed');
    source = source.slice(0, from) +
      '    check(typeof globalThis.__moaderSeedsowHold === "function", "Moader held owner is missing");\n' +
      '    const held = await globalThis.__moaderSeedsowHold({execution, acquisition, record, file});\n' +
      '    record.verified = true; record.result = held;\n' +
      source.slice(to);
    const catchFrom = source.indexOf('    if (acquisition) {\n      try {\n        const latest = await read(acquisition.report);', from);
    const catchTo = source.indexOf('    throw error;\n  } finally {', catchFrom);
    check(catchFrom >= 0 && catchTo > catchFrom &&
      source.slice(catchFrom, catchTo).includes('await acquisition.abandon()') &&
      source.slice(catchFrom, catchTo).includes('await acquisition.retire()'),
    'Pinned Seedsow failure ownership seam changed');
    source = source.slice(0, catchFrom) +
      '    if (acquisition) record.failureOwnership = {report: acquisition.report,\n' +
      '      socketPath: acquisition.socketPath, preserved: true};\n' +
      source.slice(catchTo);
  } else if (relative === 'switch2/app/inspect-mounts.cjs') {
    // Seedsow's unrelated game/unnamed entries do not form a fully walkable
    // list on this build. The acquisition only borrows the inspected save.
    source = once(source,
      '    console.log(JSON.stringify({name, accessor, implementation, service}));\n    next = pointer(bytes, 16);',
      '    console.log(JSON.stringify({name, accessor, implementation, service}));\n    if (name === "save") break;\n    next = pointer(bytes, 16);',
      'save-only mount traversal');
    source = once(source,
      'JSON.stringify({session: live.session, at: new Date().toISOString(), mounts}, null, 2)',
      'JSON.stringify({session: live.session, scope: "save-only", sdk: live.sdk, at: new Date().toISOString(), mounts}, null, 2)',
      'save-only mount proof');
    source = once(source,
      'if (require.main === module) inspect().catch(error => {console.error(error.message); process.exitCode = 1;});',
      'module.exports.moaderMain = inspect;', 'mount inspection entrypoint');
  } else if (relative === 'switch2/process/qualify.cjs') {
    source = once(source,
      "['0100c9100b06a000', '0100b4b00f9f6000'].includes(proof.app.programId)",
      `${JSON.stringify(Object.values(SEEDSOW_IDS))}.includes(proof.app.programId)`,
      'Seedsow process identity');
    source = once(source, 'The transferred current-process handle is not SmileBASIC',
      'The transferred current-process handle is not Seedsow Lullaby Demo or full game',
      'Seedsow process error');
  } else if (relative === 'switch2/ro/launch.cjs') {
    // The diagnostic launch loads this module before Moader temporarily wraps
    // Gate for the HEN launch. Resolve the export when main runs, including
    // when the qualified module is already cached from that first launch.
    source = once(source, 'const {Gate} = require("./gate.cjs");',
      'const gateModule = require("./gate.cjs");', 'dynamic Loader Gate import');
    source = once(source, '  const gate = new Gate(memory, row, state.apiWorkers, bound.waiters, server.handle, event,',
      '  const gate = new gateModule.Gate(memory, row, state.apiWorkers, bound.waiters, server.handle, event,',
      'dynamic Loader Gate construction');
  } else if (relative === 'switch2/ro/runtime.cjs') {
    // The stock RO recorded in the first SM proof has been retired by the
    // diagnostic launch. Bind the matched HEN launch to that same-session,
    // verified diagnostic PID while retaining the pinned worker start path.
    source = once(source,
      'async function activate(launchFile, memoryOverride = null) {',
      'async function activate(launchFile, memoryOverride = null, diagnosticPidOverride = null) {',
      'diagnostic RO activation argument');
    source = once(source,
      '  const preTerminated = Boolean(launch.originalTermination && launch.originalPid);',
      '  const preTerminated = diagnosticPidOverride === null && Boolean(launch.originalTermination && launch.originalPid);',
      'diagnostic RO active-state binding');
    source = once(source,
      '  const originalPid = preTerminated ? String(launch.originalPid) : smOriginalPid;',
      '  const originalPid = diagnosticPidOverride === null ?\n' +
      '    (preTerminated ? String(launch.originalPid) : smOriginalPid) : String(diagnosticPidOverride);',
      'diagnostic RO PID binding');
  } else if (relative === 'switch2/ro/nro.cjs') {
    // keepMapped deliberately leaves this process's initialized ldr:ro
    // context open. RO rejects a second Initialize for the same PID, so the
    // same-owner inspector must borrow the selected session after success.
    source = once(source,
      '    if (options.keepMapped && !operationError && record.returnValue === 0) {\n' +
      '      record.keepMapped = true;\n' +
      '      record.complete = true;\n' +
      '      await save();\n' +
      '      return {file, complete: true, returnValue: record.returnValue};\n' +
      '    }',
      '    if (options.keepMapped && !operationError && record.returnValue === 0) {\n' +
      '      record.keepMapped = true;\n' +
      '      if (live.session === "seedsow" &&\n' +
      '          (options.label === "hen-startup" || options.label === "hen-startup-mount")) {\n' +
      '        check(!globalThis.__moaderSeedsowHenRoLease, "HEN RO inspection lease already exists");\n' +
      '        globalThis.__moaderSeedsowHenRoLease = {session, handle, file, sessionId: live.session};\n' +
      '        record.inspectionLease = {handle, label: options.label};\n' +
      '      }\n' +
      '      record.complete = true;\n' +
      '      await save();\n' +
      '      return {file, complete: true, returnValue: record.returnValue};\n' +
      '    }',
      'same-owner HEN RO inspection lease');
  } else if (relative === 'switch2/ro/worker-control.cjs') {
    // The native reply parser leaves a PID descriptor as BigInt. A completed
    // RO challenge reply is saved as JSON before the held endpoint responds.
    // Keep the descriptor exact, represented as a decimal string.
    source = once(source,
      'const {packet, response} = require("../../shared/ipc.cjs");',
      'const {packet, response: parseResponse} = require("../../shared/ipc.cjs");\n' +
      'const response = bytes => {\n' +
      '  const reply = parseResponse(bytes);\n' +
      '  return {...reply, pid: reply.pid === null ? null : reply.pid.toString()};\n' +
      '};',
      'JSON-safe RO worker PID descriptor');
    // TryWaitThread can block inside pthread_exit on this Demo. The worker
    // already stores every result before exit; use those cells to admit the
    // existing owned join instead of issuing an extra native try-wait call.
    source = once(source,
      '    const finished = await s.rpc.call3(live.sdk + workerCode.code.tryWait.offset, worker.thread, 0, 0);\n' +
      '    const raw = await s.readOwned(worker.base + worker.resultOffsets[0], worker.resultOffsets.length * 8);\n' +
      '    const values = worker.logicalResultIndices.map(i => raw.readBigUInt64LE(i * 8).toString());\n' +
      '    const result = {finished: !!finished, values, sentinel: workerCode.SENTINEL.toString()};',
      '    const raw = await s.readOwned(worker.base + worker.resultOffsets[0], worker.resultOffsets.length * 8);\n' +
      '    const values = worker.logicalResultIndices.map(i => raw.readBigUInt64LE(i * 8).toString());\n' +
      '    const finished = values.length > 0 &&\n' +
      '      values.every(value => value !== workerCode.SENTINEL.toString());\n' +
      '    const result = {finished, values, sentinel: workerCode.SENTINEL.toString()};',
      'RO worker result-cell join gate');
  } else if (relative === 'switch2/ro/probe.cjs') {
    // The matched RO registers its private ro:dmnt service, clearing a stock
    // tombstone only when its first registration attempt fails.
    // Probe one owned port instead of sending five calls to dead system ports.
    source = once(source,
      "    const candidates = require('./fresh-names.cjs');\n    const areas = [0x240000, 0x1E0000, 0x180000, 0x120000, 0x0C0000];",
      "    const candidates = ['ro:dmnt'];\n    const areas = [0x240000];",
      'matched RO private-service probe');
    source = once(source,
      "      check(Date.now() < until, 'Proof worker remains pending; preserved without issuing another synchronous IPC');",
      "      if (Date.now() >= until) {\n" +
      "        proof.pendingWorker = {kind: request.kind, name: request.name, areaOffset,\n" +
      "          values: values.values, sentinel: values.sentinel};\n" +
      "        await save();\n" +
      "        throw Error('Proof worker remains pending; preserved without issuing another synchronous IPC');\n" +
      "      }",
      'pending RO worker evidence');
    const oldFallback = '  } catch (error) {\n' +
      '    try { proof.aliveAfter = await liveness(); } catch (failure) { proof.livenessError = failure.message; }';
    const start = source.indexOf(oldFallback);
    const end = source.indexOf('\n  }\n}\nmodule.exports = {main};', start);
    check(start >= 0 && end > start && source.slice(start, end).includes('proof.appError = appError.stack;'),
      'Pinned Seedsow RO fallback seam changed');
    source = source.slice(0, start) +
      '  } catch (error) {\n    proof.error = error.stack; await save();\n' +
      '    error.proofFile = file;\n    throw error;' + source.slice(end);
  } else if (relative === 'switch2/memory/probe-reserved-gap-seedsow.cjs') {
    // A zeroAfter32-only early pattern stalled during the address matrix on
    // this Demo. With weak attempts disabled, keep searching the released
    // block windows instead of letting that pattern skip all grooming rounds.
    source = once(source,
      '              range.fragmentationSkipped = range.earlyLinkCandidates.some(item =>\n' +
      '                item.encodable && (item.changedLink || item.zeroTail || item.zeroAfter32));',
      '              range.fragmentationSkipped = range.earlyLinkCandidates.some(item =>\n' +
      '                item.encodable && (item.changedLink || item.changedHeader ||\n' +
      "                  (process.env.NS2_WEAK_ATTEMPT !== '0' && (item.zeroTail || item.zeroAfter32))));",
      'strong-only early fragmentation gate');
    // This Demo measures its pin cost during the first abort-control wave.
    // The retry helper must read that measured value after a missed wave,
    // not receive the pre-wave null snapshot.
    source = once(source,
      "      () => wave('abort-control', probeReadSize), releaseInitialPin, pinBytes);",
      "      () => wave('abort-control', probeReadSize), releaseInitialPin, () => pinBytes);",
      'measured pin retry cost');
    // The candidate cost is a general FS heap counter difference. The last
    // bounded run observed one 0x7fe0-byte release during this measurement.
    // Require three matching samples on each side, then keep the exact-cost
    // guard; unstable counters stop before any alias decision.
    source = once(source, '          const beforeCandidate = await memory();',
      '          async function settledCandidateMemory(phase) {\n' +
      '            const samples = [];\n' +
      '            let previous, matches = 0;\n' +
      '            for (let sample = 0; sample < 12; sample++) {\n' +
      '              const value = await memory();\n' +
      '              samples.push(value[5]);\n' +
      '              matches = value[5] === previous ? matches + 1 : 0;\n' +
      '              if (matches >= 2) {\n' +
      '                (report.candidateCounterSamples ||= {})[phase] = samples;\n' +
      '                return value;\n' +
      '              }\n' +
      '              previous = value[5];\n' +
      '              await new Promise(resolve => setTimeout(resolve, 50));\n' +
      '            }\n' +
      '            (report.candidateCounterSamples ||= {})[phase] = samples;\n' +
      '            throw Error("Candidate allocation counter did not settle: " + phase);\n' +
      '          }\n' +
      '          const beforeCandidate = await settledCandidateMemory("before");',
      'candidate counter baseline');
    source = once(source, '          const afterCandidate = await memory();',
      '          const afterCandidate = await settledCandidateMemory("after");',
      'candidate counter result');
    source = once(source, '  finally {\n    runControl.retire();',
      '  finally {\n    if (globalThis.__moaderSeedsowGuard?.error) {\n' +
      '      report.nativeRequestUncertain = true;\n' +
      '      await save();\n' +
      '      await globalThis.__moaderSeedsowPreserve();\n' +
      '    }\n    runControl.retire();', 'uncertain probe ownership');
    source = once(source,
      'main().catch(error => {console.error(error.stack); process.exitCode = 1;});',
      'module.exports.moaderMain = main;', 'probe entrypoint');
  } else if (relative === 'switch2/memory/pinning-control.cjs') {
    source = once(source,
      '      if (!(error instanceof PinningWindowMiss) || !retryablePinningMiss(error.row, pinBytes)) throw error;',
      '      const measuredPinBytes = typeof pinBytes === "function" ? pinBytes() : pinBytes;\n' +
      '      if (!(error instanceof PinningWindowMiss) || !retryablePinningMiss(error.row, measuredPinBytes)) throw error;',
      'deferred measured pin retry cost');
  } else if (relative === 'switch2/retained/cache-shape.cjs') {
    // The pinned positive-canary input fills its path with B bytes. Admit an
    // exact all-B unused tail only when this same report proves that wave
    // changed the canary. The old zero/A-path cases remain unchanged.
    source = once(source, 'function unusedTail(bytes) {',
      'function unusedTail(bytes, report) {', 'qualified positive fill argument');
    source = once(source, '  if (!tail.some(Boolean)) return true;',
      '  if (!tail.some(Boolean)) return true;\n' +
      '  if (report?.canaryChanged === true && report?.markerSelected === true &&\n' +
      '      Array.isArray(report.cases) && report.cases.some(row =>\n' +
      '        row.profile === "positive" && row.allPending === true && row.canaryType === 0 &&\n' +
      '        Array.isArray(row.changedPaths) && row.changedPaths.length === 1 &&\n' +
      '        row.changedPaths[0] === "canary") && tail.every(byte => byte === 0x42)) return true;',
      'qualified positive fill tail');
  } else if (relative === 'switch2/retained/retained-memory.cjs') {
    source = once(source,
      'check(require("./cache-shape.cjs").unusedTail(before),',
      'check(require("./cache-shape.cjs").unusedTail(before, context.report),',
      'retained positive fill proof');
  } else if (relative === 'switch2/memory/session.cjs') {
    source = once(source, '  catch (error) { await s.cleanup(); throw error; }',
      '  catch (error) {\n' +
      '    if (globalThis.__moaderSeedsowGuard?.error) await globalThis.__moaderSeedsowPreserve();\n' +
      '    await s.cleanup(); throw error;\n' +
      '  }', 'pre-acquisition ownership');
  } else if (relative === 'switch2/memory/run-control.cjs') {
    source = once(source, "    process.on('SIGINT', this.stopSignal);\n    process.on('SIGTERM', this.stopSignal);",
      '    // Moader owns signals and preserves native state on interruption.',
      'probe signal ownership');
    source = once(source, "    process.off('SIGINT', this.stopSignal);\n    process.off('SIGTERM', this.stopSignal);",
      '    // The producer retains the only signal handlers.', 'probe signal release');
  } else if (STAGE_ENTRYPOINTS.has(relative)) {
    if (relative === 'switch2/ro/install-retained.cjs')
      source = once(source, 'unusedTail(bytes.subarray(at, at + 0x5a0))',
        'unusedTail(bytes.subarray(at, at + 0x5a0), report)',
        'captured positive fill proof');
    if (relative === 'switch2/app/map.cjs' || relative === 'switch2/ro/install-retained.cjs') {
      // Its conditional CLI entrypoint is inert under the private loader.
      check(source.includes('if (require.main === module) main().catch('),
        'Pinned Seedsow map entrypoint changed');
    } else {
      // For stage files with unconditional CLI entrypoints, remove only the
      // final call. Keep the full reviewed implementation and export main.
      const at = source.lastIndexOf('\nmain().catch(');
      check(at >= 0 && source.slice(at).trim().startsWith('main().catch(') &&
        source.slice(at).trim().endsWith('});'),
      'Pinned Seedsow stage entrypoint changed: ' + relative);
      source = source.slice(0, at + 1);
    }
    if (relative === 'switch2/ro/test-sm.cjs' ||
        relative === 'switch2/ro/copy-loader-root.cjs') {
      source = once(source, 'async function main() {', 'async function main(moaderArgument) {',
        relative + ' argument entrypoint');
      source = once(source, 'Number(process.argv[2])', 'Number(moaderArgument)',
        relative + ' argument source');
    }
    source += '\nmodule.exports.moaderMain = main;\n';
  }
  return {source, sha256: sha(Buffer.from(source)), changed: source !== original};
}
module.exports = {transform, STAGE_ENTRYPOINTS, SEEDSOW_IDS};
