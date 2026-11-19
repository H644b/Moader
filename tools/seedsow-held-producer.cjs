// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// One-process producer for the pinned Seedsow non-HEN 4/4 path. The source
// closure is privately materialized before this adapter runs. A single probe
// promise owns the live endpoint; stage calls share its console RPC guard.
const fs = require('node:fs/promises');
const fssync = require('node:fs');
const path = require('node:path');
const check = (ok, message) => { if (!ok) throw Error(message); };
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const STAGES = new Set([
  'switch2/app/map.cjs', 'switch2/app/inspect-mounts.cjs',
  'switch2/retained/find-metadata.cjs',
  'switch2/ro/install-retained.cjs', 'switch2/ro/test-sm.cjs',
  'switch2/ro/copy-loader-root.cjs', 'switch2/ro/prepare-workers.cjs',
  'switch2/ro/bind-sessions.cjs', 'switch2/ro/prepare-event.cjs',
  'switch2/ro/launch.cjs', 'switch2/process/bind-notifier.cjs',
  'switch2/process/qualify.cjs',
]);
const FORBIDDEN_ENV = Object.freeze([
  'NS2_STOP_AFTER_STAGE', 'NS2_STOP_AFTER_ACQUIRE', 'NS2_STOP_AFTER_LAUNCH',
  'NS2_HOLD_TYPES', 'NS2_FS_PROBE', 'NS2_FS_PROBE_ONLY', 'NS2_LAUNCH_WAIT_ONLY',
  'NS2_LAUNCH_WATCH', 'NS2_RO_PROBE', 'NS2_QUALIFY', 'NS2_HEN_RETIRE',
  'NS2_HEN_DETACH',
]);
function checkEnvironment(env = process.env) {
  for (const key of FORBIDDEN_ENV) check(!env[key] || env[key] === '0',
    'Seedsow held producer rejects ' + key);
  check(!env.NS2_HEN || env.NS2_HEN === '0', 'Seedsow FS proof must use the non-HEN path');
  check(env.NS2_SKIP_TEST_GATE === '1', 'Seedsow held producer requires the reviewed gate path');
  check(env.NS2_MAIN_SWAP === '1' && env.NS2_MAIN_SWAP_MODE === 'capture',
    'Seedsow held producer requires the reviewed main redirect');
  check(env.NS2_POST_START_OPEN === '0' && env.NS2_OPEN_SWAP === '0' &&
    env.NS2_CODE_SWAP === '0' && env.NS2_FS_SWAP === '0',
  'Seedsow held producer requires the reviewed launch options');
  check(env.NS2_GROOM_ROUNDS === '4' && env.NS2_ALIAS_BUCKETS === '5' &&
    env.NS2_ALIAS_OFFSET === '0x13b0000' && env.NS2_FAST === '1' &&
    env.NS2_WAVE_SETTLE_MS === '1000' && env.NS2_POSITIVE_ATTEMPTS === '1' &&
    env.NS2_SKIP_REPRO === '1' &&
    env.NS2_WEAK_ATTEMPT === '0' && env.NS2_STABLE_ATTEMPT === '0',
  'Seedsow held producer requires the qualified bounded probe settings');
}
function create({run, guard, endpoint}) {
  check(typeof run === 'string' && path.isAbsolute(run) && fssync.realpathSync(run) === run &&
    /^switch-[A-Za-z0-9]+$/.test(path.basename(run)), 'Expected a fresh canonical Seedsow run');
  check(guard && endpoint?.guard === guard && typeof endpoint.bind === 'function',
    'Seedsow producer needs the shared guarded lazy endpoint');
  let loader = null, probePromise = null, probeSettled = false, probeError = null;
  const owner = {run, guard, endpoint, get loader() { return loader; },
    get probePromise() { return probePromise; }, get probeSettled() { return probeSettled; }};
  function attach(value) {
    check(!loader && value?.run === run && typeof value.load === 'function',
      'Seedsow producer source loader differs');
    loader = value;
  }
  // If a reply is lost, leave the application, endpoint, and in-process probe
  // alive for inspection. An unresolved Promise alone would not keep Node up.
  let preserveTimer = null;
  function preserve() {
    if (!preserveTimer) preserveTimer = setInterval(() => {}, 60000);
    return new Promise(() => {});
  }
  globalThis.__moaderSeedsowGuard = guard;
  globalThis.__moaderSeedsowPreserve = preserve;

  async function stage(directory, name, args = [], timeout = 10 * 60 * 1000) {
    check(loader, 'Seedsow source loader is not attached');
    check(typeof directory === 'string' && typeof name === 'string' &&
      Array.isArray(args) && args.every(value => typeof value === 'string' ||
        typeof value === 'number') && Number.isSafeInteger(timeout) && timeout > 0,
    'Invalid Seedsow stage');
    const relative = path.relative(run, path.join(directory, name + '.cjs')).split(path.sep).join('/');
    check(STAGES.has(relative), 'Unreviewed Seedsow stage: ' + relative);
    if (guard.error) throw guard.error;
    if (relative === 'switch2/process/bind-notifier.cjs') return null;
    const module = loader.load(relative), main = module.moaderMain || module.main;
    check(typeof main === 'function', 'Pinned Seedsow stage has no controlled entrypoint: ' + relative);
    let timer;
    try {
      const result = await Promise.race([main(...args), new Promise((_, reject) => {
        timer = setTimeout(() => reject(guard.fail(Error('Seedsow stage deadline expired: ' + relative +
          '; native result is uncertain and the owner is retained'))), timeout);
      })]);
      if (guard.error) throw guard.error;
      if (relative === 'switch2/app/inspect-mounts.cjs')
        return loader.qualifyMounts();
      if (!result?.file) return result || null;
      check(typeof result.file === 'string' && result.file.startsWith(run + path.sep) &&
        fssync.realpathSync(result.file) === result.file && fssync.lstatSync(result.file).isFile(),
      'Seedsow stage report escaped its run');
      const proof = JSON.parse(await fs.readFile(result.file, 'utf8'));
      return {...proof, file: result.file, summary: result};
    } catch (error) { throw guard.fail(error); }
    finally { clearTimeout(timer); }
  }

  async function start() {
    checkEnvironment();
    check(loader && !probePromise, 'Seedsow acquisition can start only once from the source loader');
    const directory = path.join(run, 'switch2/memory');
    const before = new Set(await fs.readdir(directory));
    const probe = loader.load('switch2/memory/probe-reserved-gap-seedsow.cjs');
    check(typeof probe.moaderMain === 'function', 'Seedsow acquisition source is not controlled');
    probePromise = Promise.resolve().then(() => probe.moaderMain());
    probePromise.then(() => { probeSettled = true; }, error => {
      probeSettled = true; probeError = error;
    });
    const deadline = Date.now() + 45 * 60 * 1000;
    let reportFile = null, report = null, transientReads = 0;
    for (;;) {
      if (guard.error) throw guard.error;
      const candidates = (await fs.readdir(directory)).filter(name =>
        /^reserved-gap-[0-9]+\.json$/.test(name) && !before.has(name));
      check(candidates.length <= 1, 'Seedsow acquisition created multiple reports');
      if (candidates.length) {
        reportFile = path.join(directory, candidates[0]);
        try {
          report = JSON.parse(await fs.readFile(reportFile, 'utf8'));
          transientReads = 0;
        } catch (error) {
          check((error instanceof SyntaxError ||
            ['ENOENT', 'EPERM', 'EACCES', 'EBUSY'].includes(error.code)) &&
            ++transientReads <= 20,
          'Seedsow acquisition report could not be read consistently: ' + error.message);
          await delay(100);
          continue;
        }
        if (report.memoryControl?.ready) break;
      }
      if (probeSettled || Date.now() >= deadline) {
        throw Error('Seedsow single acquisition did not reach held memory-ready: ' +
          (report?.error || probeError?.message || report?.stopped || 'deadline'));
      }
      await delay(500);
    }
    check(report.session === 'seedsow' && !report.error && !report.cleanupError &&
      !report.placementUncertain && !report.memoryControl?.pending,
    'Seedsow acquisition report is not ready and uniquely owned');
    endpoint.bind(report.memoryControl.socketPath);
    const proof = await loader.load('switch2/memory/verify-proof.cjs').verifyProof(reportFile, 'seedsow');
    const supervisorReport = path.join(run, 'switch2/acquisition-supervisor.json');
    await fs.writeFile(supervisorReport, JSON.stringify({stage: 'memory-ready', attempts: [{report: reportFile}]}) + '\n',
      {flag: 'wx', mode: 0o600});
    const current = {session: 'seedsow', report: reportFile, supervisorReport,
      socketPath: report.memoryControl.socketPath, proof, finished: new Date().toISOString()};
    await fs.writeFile(path.join(run, 'switch2/current-acquisition.json'),
      JSON.stringify(current, null, 2) + '\n', {flag: 'wx', mode: 0o600});
    return current; // Deliberately no retire(), abandon(), kill(), or retry().
  }
  return Object.freeze({owner, attach, stages: Object.freeze({run: stage}),
    acquire: Object.freeze({start}), preserve});
}
module.exports = {create, checkEnvironment, STAGES, FORBIDDEN_ENV};
