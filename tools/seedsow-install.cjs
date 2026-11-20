#!/usr/bin/env node
// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// One Moader-owned Seedsow Lullaby attempt. --check performs source/build/run
// graph qualification without touching the console. --start starts one saved
// launcher RPC loop, one acquisition, the restored FS challenge, and then the
// Moader installer in the same owner process. A proven success retires the
// retained cache and listener once; uncertain results retain the owner.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const ENV_SHA = 'b0d58c43c05ff707ce3429fa4392139cc554ad0ee2cecb7e9b3cc1f598d86bd4';
// programs/ounceshell.nro in the checksum-pinned official kaiHEN 20261117.1 archive.
const OFFICIAL_OUNCESHELL = Object.freeze({
  bytes: 22294859,
  sha256: 'e506fab96b6031831cfbf3cf9459ed70f0bda92f6d619f8230b8e0a000165735',
});
const ROOT = path.resolve(__dirname, '..');
function args(argv) {
  check(argv.length === 7 && ['--check', '--start'].includes(argv[0]) &&
    argv[1] === '--console' && argv[3] === '--preflight' && argv[5] === '--upstream-env',
  'Usage: node tools/seedsow-install.cjs --check|--start --console IPv4 --preflight REPORT --upstream-env PINNED_ENV');
  return {mode: argv[0], console: argv[2], preflight: path.resolve(argv[4]),
    upstreamEnv: path.resolve(argv[6])};
}
function qualifyHenInspection(inspection, henReceipt) {
  // The bootstrap overwrites its PM PID slot for each launched plugin. After
  // the plugin loop, henReceipt.hen.pid is the last plugin PID, not HEN's PID.
  // Bind the inspection to the exact retained startup file and require a
  // healthy HEN and resident reply without equating those distinct processes.
  check(inspection.complete === true && inspection.controllerHandlesClosed === true &&
    inspection.resourcesRetained === false && inspection.borrowedRoRetained === true &&
    inspection.borrowedRo?.sessionId === 'seedsow' &&
    inspection.borrowedRo.file === henReceipt.hen?.file &&
    typeof inspection.hen?.pid === 'string' && /^[1-9][0-9]*$/.test(inspection.hen.pid) &&
    inspection.hen.result === 0 && inspection.plugins?.enumerationComplete === true &&
    inspection.plugins.names?.includes('moader') && inspection.moader?.listed === true &&
    inspection.moader.resident === true && inspection.moader.serviceReachable === true &&
    inspection.moader.summary?.backendConnected === true &&
    inspection.moader.summary.routingGenerationCurrent === true &&
    inspection.game?.phase === 3 && inspection.game.result === 0,
  'Current Seedsow HEN/Moader service and game arm did not pass the same-owner status check');
  return true;
}
function applyEnvironment(file) {
  check(fs.realpathSync(file) === file && fs.lstatSync(file).isFile(),
    'Seedsow upstream run environment must be a canonical file');
  const bytes = fs.readFileSync(file);
  check(sha(bytes) === ENV_SHA, 'Seedsow upstream run environment differs from the pinned source');
  const seen = new Set();
  for (const line of bytes.toString('utf8').split(/\r?\n/)) {
    if (!line.trim() || line.trimStart().startsWith('#')) continue;
    const match = /^(NS2_[A-Z0-9_]+)=([^\r\n]*)$/.exec(line);
    check(match && !seen.has(match[1]), 'Unsupported or duplicate Seedsow run environment');
    seen.add(match[1]); process.env[match[1]] = match[2];
  }
  Object.assign(process.env, {
    NS2_HEN: '0', NS2_HEN_RETIRE: '0', NS2_HEN_DETACH: '0',
    NS2_ACQUIRE_ATTEMPTS: '1', NS2_HEN_KEEP_MAPPED: '1', NS2_HEN_HOST_PACKAGE: '0',
    // The pinned probe supports up to four independent release windows. Use
    // that bound after qualifying the unchanged upstream environment file.
    NS2_GROOM_ROUNDS: '4',
    // The pinned probe lists five measured alias offsets but its default
    // budget tries only three. Search the full supported list on this boot.
    NS2_ALIAS_BUCKETS: '5',
    // The successful regular GUI run used this offset and these worker/wave
    // settings. Put that bucket first while retaining the five-offset list.
    NS2_ALIAS_OFFSET: '0x13b0000', NS2_FAST: '1', NS2_WAVE_SETTLE_MS: '1000',
    // This Demo's successful boots selected on guarded wave one. A later
    // retry froze in an FS commit worker; stop a miss before that dispatch.
    NS2_POSITIVE_ATTEMPTS: '1',
    // After one strict canary selection, the pinned probe can use the
    // independent alias-content check instead of another positive worker
    // series. A reproduction retry left a native worker pending on this Demo.
    NS2_SKIP_REPRO: '1',
    NS2_WEAK_ATTEMPT: '0',
  });
  require('./seedsow-held-producer.cjs').checkEnvironment();
}
function createRun() {
  const base = path.join(ROOT, 'build/seedsow-runs');
  fs.mkdirSync(base, {recursive: true, mode: 0o700});
  for (let i = 0; i < 10; i++) {
    const run = path.join(base, 'switch-' + crypto.randomBytes(6).toString('hex'));
    try { fs.mkdirSync(run, {mode: 0o700}); return fs.realpathSync(run); }
    catch (error) { if (error.code !== 'EEXIST') throw error; }
  }
  throw Error('Could not create a unique Seedsow run');
}
function qualifyOfficialShell(prepared) {
  const shells = prepared.bundledPrograms?.filter(item => item.target === '/switch/ounceshell.nro');
  check(shells?.length === 1 && shells[0].bytes === OFFICIAL_OUNCESHELL.bytes &&
    shells[0].sha256 === OFFICIAL_OUNCESHELL.sha256,
  'Seedsow requires the complete official kaiHEN program set');
  return shells[0];
}
async function main(argv = process.argv.slice(2)) {
  check(process.version.startsWith('v24.') && ['darwin', 'linux'].includes(process.platform),
    'Seedsow installation requires POSIX Node.js 24');
  const options = args(argv);
  applyEnvironment(options.upstreamEnv);
  const hosts = require('./host-session.cjs');
  hosts.qualifySources(hosts.resolveProfile('seedsow-malev0'));
  const install = require('./install-moader.cjs');
  const prepared = await install.prepare('seedsow-malev0');
  qualifyOfficialShell(prepared);
  check(typeof prepared.kaihen === 'string' &&
    fs.realpathSync(prepared.kaihen) === prepared.kaihen &&
    fs.lstatSync(prepared.kaihen).isDirectory(),
  'Seedsow RO donor requires the qualified kaiHEN snapshot');
  process.env.KAIHEN_DIR = prepared.kaihen;
  const run = createRun();
  console.log('MOADER_RUN ' + run);
  const source = require('./seedsow-source-loader.cjs');
  source.materializeHeld({root: ROOT, run});
  const transport = require('./seedsow-transport.cjs');
  const guard = transport.createGuard();
  const endpoint = require('./seedsow-lazy-endpoint.cjs').create(guard);
  const producer = require('./seedsow-held-producer.cjs').create({run, guard, endpoint});
  if (options.mode === '--start') {
    const retainUnexpected = (reason, kind) => {
      const error = guard.fail(reason instanceof Error ? reason : Error(String(reason)));
      console.error(kind + ': ' + (error.stack || error));
      console.error('Seedsow owner and evidence retained in ' + run);
      producer.preserve();
    };
    process.on('unhandledRejection', reason => retainUnexpected(reason, 'Unhandled Seedsow rejection'));
    process.on('uncaughtException', error => retainUnexpected(error, 'Uncaught Seedsow exception'));
    for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => {
      const error = guard.fail(Error('Seedsow owner received ' + signal +
        '; native result and held state require inspection'));
      console.error(error.message);
    });
    fs.writeFileSync(path.join(run, 'moader-listener-attempt.json'),
      JSON.stringify({scope: 'one launcher listener, one acquisition, one held installation',
        ownerPid: process.pid, console: options.console, preflight: options.preflight,
        preparedManifest: prepared.manifestFile, groomRounds: Number(process.env.NS2_GROOM_ROUNDS),
        aliasBuckets: Number(process.env.NS2_ALIAS_BUCKETS),
        aliasOffset: process.env.NS2_ALIAS_OFFSET, fastWorker: true,
        waveSettleMs: Number(process.env.NS2_WAVE_SETTLE_MS),
        weakAttempt: false,
        started: new Date().toISOString()}, null, 2) + '\n',
      {flag: 'wx', mode: 0o600});
  }
  let live;
  if (options.mode === '--start') {
    try {
      live = await require('./seedsow-listener.cjs').open({host: options.console,
        run, preflightFile: options.preflight, guard});
    } catch (error) {
      if (guard.error) {
        console.error('Seedsow listener result is uncertain; preserving owner and ' + run);
        await producer.preserve();
      }
      throw error;
    }
  }
  const rpc = options.mode === '--start'
    ? require('./seedsow-shared-rpc.cjs').create({rpc: live.rpc,
      connectionFile: live.connectionFile, connectionSha256: live.connectionSha256, run})
    : {a: {platform: 2, session: 'seedsow', sdk: 0x1000, main: 0x2000, scratch: 0x3000},
      guard, request() { throw Error('Offline Seedsow graph attempted native RPC'); }};
  let loader, pipeline;
  try {
    loader = source.create({root: ROOT, run, rpc, endpoint, heldProducer: true,
      stages: producer.stages, acquire: producer.acquire});
    producer.attach(loader);
    pipeline = loader.load('switch2/pipeline.cjs');
  } catch (error) {
    if (live) {
      guard.fail(error);
      console.error('Seedsow listener is owned; preserving after source import failure in ' + run);
      await producer.preserve();
    }
    throw error;
  }
  if (options.mode === '--check') {
    check(typeof pipeline.main === 'function' && Object.keys(loader.materialized()).length ===
      Object.keys(source.TRANSFORMED).length,
    'Seedsow controlled source graph is incomplete');
    console.log(JSON.stringify({scope: 'offline-only', run, sourceSha256: loader.sourceSha256,
      transformed: loader.materialized(), preparedManifest: prepared.manifestFile,
      consoleContacted: false}, null, 2));
    return;
  }
  globalThis.__moaderSeedsowHold = async ({execution, acquisition}) => {
    check(guard.error === null && acquisition.socketPath === endpoint.socketPath,
      'Seedsow FS handoff lost its original endpoint');
    const info = await endpoint.request({command: 'retained-info'});
    check(info.ready === true && info.steadyStateReliable === true &&
      info.requiresCorruption === false && info.report === acquisition.report &&
      info.session === 'seedsow', 'Seedsow retained endpoint did not confirm the current FS owner');
    const host = require('./seedsow-provider.cjs').admitHeld({run, root: ROOT, loader,
      guard, endpoint, owner: producer.owner, execution,
      connectionSha256: live.connectionSha256});
    check(host.target === 'seedsow', 'Seedsow FS proof did not admit its own installer');
    await install.main(['--prepared', prepared.manifestFile, run]);
    const henReceipt = JSON.parse(fs.readFileSync(path.join(ROOT, 'evidence/kaihen',
      'install-' + path.basename(run) + '.json')));
    const borrowedRo = globalThis.__moaderSeedsowHenRoLease;
    check(henReceipt.complete === true && borrowedRo?.sessionId === 'seedsow' &&
      borrowedRo.file === henReceipt.hen?.file &&
      borrowedRo.file.startsWith(path.join(run, 'switch2/ro') + path.sep) &&
      borrowedRo.session?.handles?.has(borrowedRo.handle),
    'Matched HEN startup did not retain its owned RO inspection session');
    const startup = JSON.parse(fs.readFileSync(borrowedRo.file));
    check(startup.complete === true && startup.keepMapped === true &&
      startup.inspectionLease?.handle === borrowedRo.handle &&
      startup.pid === henReceipt.hen.pid,
    'Retained HEN RO inspection lease differs from the completed startup');
    const inspector = require('./inspect-hen.cjs');
    let inspection;
    try {
      inspection = await inspector.inspect({
        Session: loader.load('switch2/app/session.cjs').Session,
        packet: loader.load('shared/ipc.cjs').packet,
        argumentsWithPid: loader.load('switch2/ro/nro.cjs').argumentsWithPid,
        protocol: await inspector.readProtocol(), transportGuard: guard, run,
        borrowedRo,
      });
    } catch (error) {
      inspection = error.inspection || {complete: false, error: String(error.stack || error)};
      fs.writeFileSync(path.join(run, 'moader-hen-inspection.json'),
        JSON.stringify(inspection, null, 2) + '\n', {flag: 'wx', mode: 0o600});
      throw error;
    }
    fs.writeFileSync(path.join(run, 'moader-hen-inspection.json'),
      JSON.stringify(inspection, null, 2) + '\n', {flag: 'wx', mode: 0o600});
    qualifyHenInspection(inspection, henReceipt);
    console.log('Moader service and game arm passed; retiring owned FS cache and listener: ' + run);
    let retirement;
    try {
      retirement = await require('./seedsow-success-retire.cjs').retire({
        acquisition, endpoint, producer, guard, run, listener: live});
      fs.writeFileSync(path.join(run, 'moader-success-retirement.json'),
        JSON.stringify(retirement, null, 2) + '\n', {flag: 'wx', mode: 0o600});
    } catch (error) { throw guard.fail(error); }
    const record = {scope: 'Moader installation completed; physical app/resident/FS acceptance pending',
      run, at: new Date().toISOString(), ownerPid: process.pid,
      nativeRequestUncertain: false, sourceSha256: loader.sourceSha256,
      henInspection: path.join(run, 'moader-hen-inspection.json'),
      retirement: path.join(run, 'moader-success-retirement.json')};
    fs.writeFileSync(path.join(run, 'moader-device-verification-pending.json'),
      JSON.stringify(record, null, 2) + '\n', {flag: 'wx', mode: 0o600});
    console.log('Moader setup complete; FS cache restored and listener closed: ' + run);
    return {execution, retirement, deviceVerificationPending: true};
  };
  try { await pipeline.main(); }
  catch (error) {
    console.error(error.stack || error);
    const failure = require('./seedsow-failure-stop.cjs').classify(run,
      {probeSettled: producer.owner.probeSettled, guardError: guard.error});
    if (failure.eligible) {
      try {
        const listener = await live.stopAfterProvenFailure();
        const receipt = {schema: 1, run, at: new Date().toISOString(),
          classification: failure, listener, hostOwnerExiting: true,
          nativeCleanup: failure.nativeCleanup};
        fs.writeFileSync(path.join(run, 'moader-failure-stop.json'),
          JSON.stringify(receipt, null, 2) + '\n', {flag: 'wx', mode: 0o600});
        console.log('Seedsow definite pre-FS miss: owned listener stopped; host owner exiting. ' +
          'Native handles and scratch release when Seedsow exits or the Switch reboots. ' + run);
        process.exitCode = 1;
        return;
      } catch (stopError) {
        guard.fail(stopError);
        console.error('Seedsow failure stop is uncertain: ' + (stopError.stack || stopError));
      }
    } else console.error('Seedsow failure stop refused: ' + failure.reason);
    console.error('Seedsow owner and evidence retained in ' + run + '; inspect before any new native work.');
    await producer.preserve();
  }
}
module.exports = {main, args, applyEnvironment, createRun, qualifyHenInspection, qualifyOfficialShell};
if (require.main === module) main().catch(error => { console.error(error.stack); process.exitCode = 1; });
