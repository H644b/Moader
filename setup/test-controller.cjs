// SPDX-License-Identifier: GPL-2.0-only
// Host-only controller tests. No address in this file is contacted.
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const policy = require('./host-policy.cjs');
const hostTools = require('./host-tools.cjs');

const root = path.resolve(__dirname, '..');
const releaseSpec = JSON.parse(fs.readFileSync(path.join(root, 'config/release.json')));
const releaseInputsPresent = () => Object.values(releaseSpec.binaries).concat([
  releaseSpec.patched_hen, releaseSpec.patched_ro,
  releaseSpec.patched_bootstrap]).every(x => fs.existsSync(path.join(root, x.source)));
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function startController(t, initialState = null, extraEnv = {}, kit = root) {
  const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'moader-setup-test-'));
  if (initialState) await fsp.writeFile(path.join(dir, 'controller-state.json'),
    JSON.stringify(initialState));
  const includedNode = path.join(kit, 'host-runtimes/node-v24.22.3',
    process.platform + '-' + process.arch, 'node');
  const executable = fs.existsSync(includedNode) ? includedNode : process.execPath;
  const child = spawn(executable, [path.join(kit, 'setup/controller.cjs')], {
    cwd: kit, env: {...process.env, MOADER_PYTHON: process.env.MOADER_PYTHON || 'python3', MOADER_SETUP_STATE_DIR: dir,
      MOADER_SETUP_NO_OPEN: '1', ...extraEnv}, stdio: 'ignore'});
  t.after(async () => {
    child.kill('SIGTERM');
    await sleep(100);
    await fsp.rm(dir, {recursive: true, force: true});
  });
  let lock;
  for (let i = 0; i < 100; i++) {
    try { lock = JSON.parse(await fsp.readFile(path.join(dir, 'controller.json'))); break; }
    catch { if (child.exitCode !== null) throw Error('Controller stopped before opening.'); await sleep(50); }
  }
  assert.ok(lock, 'controller started');
  const origin = 'http://127.0.0.1:' + lock.port;
  const get = async (url, opts = {}) => fetch(origin + url, {
    ...opts, headers: {'X-Moader-Token': lock.token, ...opts.headers}});
  const post = (url, value, headers = {}) => get(url, {method: 'POST',
    headers: {'Origin': origin, 'Content-Type': 'application/json', ...headers},
    body: JSON.stringify(value)});
  return {dir, child, origin, lock, get, post};
}
async function untilDone(api) {
  for (let i = 0; i < 100; i++) {
    const state = await (await api.get('/api/state')).json();
    if (state.operation && state.operation.status !== 'running') return state;
    await sleep(50);
  }
  throw Error('Operation did not settle.');
}

test('host policy rejects unbundled architectures and native Windows', () => {
  assert.equal(policy.supported('seedsow', 'darwin', 'arm64'), true);
  assert.equal(policy.supported('seedsow', 'darwin', 'x64'), false);
  assert.equal(policy.supported('seedsow', 'linux', 'x64'), true);
  assert.equal(policy.supported('seedsow', 'linux', 'arm64'), false);
  assert.equal(policy.supported('mac2jb', 'linux', 'x64'), false);
  assert.equal(policy.supported('ns2jb', 'linux', 'arm64'), true);
  assert.equal(policy.supported('civswitch', 'win32', 'x64'), false);
  assert.equal(policy.supported('ns2jb', 'linux', 'riscv64'), false);
});

test('Linux tool handoff keeps WSL names and executable paths as arguments', () => {
  const apt = hostTools.managers.find(item => item.name === 'apt');
  const steps = hostTools.plan({wsl: true, distro: 'Ubuntu é test',
    wslExe: '/mnt/c/Windows/System32/wsl.exe', manager: apt,
    packages: ['git', 'clang', 'zstd']});
  assert.equal(steps.length, 2);
  assert.deepEqual(steps[0].args,
    ['-d', 'Ubuntu é test', '-u', 'root', '--', '/usr/bin/apt-get', 'update']);
  assert.deepEqual(steps[1].args,
    ['-d', 'Ubuntu é test', '-u', 'root', '--', '/usr/bin/apt-get',
      'install', '-y', '--no-install-recommends', 'git', 'clang', 'zstd']);
  assert.deepEqual(hostTools.plan({wsl: false, pkexec: '/usr/bin/pkexec',
    manager: apt, packages: ['git']})[1].args,
  ['/usr/bin/apt-get', 'install', '-y', '--no-install-recommends', 'git']);
  assert.throws(() => hostTools.plan({wsl: true, distro: 'bad\nname',
    wslExe: 'wsl.exe', manager: apt, packages: ['git']}), /selected WSL2/);
  assert.throws(() => hostTools.plan({wsl: false, manager: apt,
    packages: ['git']}), /graphical administrator/);
});

test('wrong component hash is shown before a device action', async t => {
  if (!releaseInputsPresent()) return t.skip('requires matched local release inputs');
  const kit = await fsp.mkdtemp(path.join(os.tmpdir(), 'moader-setup-kit-'));
  t.after(() => fsp.rm(kit, {recursive: true, force: true}));
  const release = JSON.parse(await fsp.readFile(path.join(root, 'config/release.json')));
  const files = ['config/release.json', 'setup/controller.cjs', 'setup/host-policy.cjs',
    'setup/host-tools.cjs',
    ...Object.values(release.binaries).map(x => x.source),
    release.patched_hen.source, release.patched_ro.source,
    release.patched_bootstrap.source];
  for (const name of files) {
    const destination = path.join(kit, name);
    await fsp.mkdir(path.dirname(destination), {recursive: true});
    await fsp.copyFile(path.join(root, name), destination);
  }
  const damaged = path.join(kit, release.binaries['Moader.nro'].source);
  const image = await fsp.readFile(damaged);
  image[image.length - 1] ^= 1;
  await fsp.writeFile(damaged, image);
  const api = await startController(t, null, {}, kit);
  const state = await (await api.get('/api/state')).json();
  assert.equal(state.kit.ready, false);
  assert.ok(state.kit.wrong.includes(release.binaries['Moader.nro'].source));
  const noTools = await startController(t, null, {PATH: path.join(kit, 'missing-bin')}, kit);
  assert.equal((await noTools.post('/api/operation',
    {action: 'export-sd', kind: 'app'})).status, 202);
  const failed = await untilDone(noTools);
  assert.equal(failed.operation.status, 'failed');
  assert.match(failed.operation.firstError, /Python preparation helper is missing/i);
});

test('loopback token, origin, kit identity, SD export and support copy', async t => {
  if (!releaseInputsPresent()) return t.skip('requires matched local release inputs');
  const api = await startController(t);
  assert.equal((await fetch(api.origin + '/api/state')).status, 400);
  assert.equal((await api.post('/api/preferences', {}, {Origin: 'https://elsewhere.invalid'})).status, 400);
  assert.equal((await api.get('/favicon.svg')).status, 200);
  const first = await (await api.get('/api/state')).json();
  assert.equal(first.kit.ready, true, 'matched local release inputs are present');
  assert.equal(first.operation, null);
  assert.equal((await api.post('/api/check-launcher',
    {address: 'not-an-ip', edition: 'demo'})).status, 400);
  const failedLauncher = await (await api.get('/api/state')).json();
  assert.equal(failedLauncher.launcher.online, false);
  assert.match(failedLauncher.launcher.error, /private LAN IPv4/);
  assert.equal((await api.post('/api/capture/import', {files: {}})).status, 400);
  const wrongCapture = Object.fromEntries(['fs-4.bin', 'manifest.json',
    'moader-success-retirement.json', 'moader-device-verification-pending.json',
    'moader-install.json'].map(name => [name, Buffer.from('wrong').toString('base64')]));
  assert.equal((await api.post('/api/capture/import',
    {files: wrongCapture})).status, 400);
  assert.equal(first.standaloneReady, false);
  assert.equal((await api.post('/api/operation',
    {action: 'generate-standalone'})).status, 202);
  const noCapture = await untilDone(api);
  assert.equal(noCapture.operation.status, 'failed');
  assert.equal(noCapture.operation.nativeStarted, false);
  assert.equal((await api.post('/api/operation', {action: 'export-sd', kind: 'app'})).status, 202);
  const done = await untilDone(api);
  assert.equal(done.operation.status, 'complete');
  assert.equal(done.operation.nativeStarted, false);
  assert.deepEqual(done.sdPlan.files.map(x => x.path),
    ['kaihen/plugins/moader.nso', 'switch/Moader.nro']);
  for (let i = 0; i < done.sdPlan.files.length; i++) {
    const response = await api.get('/api/stage-file?index=' + i);
    assert.equal(response.status, 200);
    const bytes = Buffer.from(await response.arrayBuffer());
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),
      done.sdPlan.files[i].sha256);
  }
  assert.equal((await api.post('/api/stage-result',
    {planId: 'wrong', kind: 'app', verified: 2, complete: true})).status, 400);
  assert.equal((await api.post('/api/stage-result',
    {planId: done.sdPlan.id, kind: 'app', verified: 0, complete: false,
      error: 'SD readback differs: switch/Moader.nro'})).status, 200);
  const incomplete = await (await api.get('/api/state')).json();
  assert.match(incomplete.sdResult.error, /SD readback differs/);
  assert.equal(incomplete.sdResult.complete, false);
  assert.equal((await api.post('/api/stage-result',
    {planId: done.sdPlan.id, kind: 'app', verified: 2, complete: true})).status, 200);
  const report = await (await api.get('/api/report')).json();
  assert.equal(report.nativeOutcome, 'never-started');
  assert.ok(report.providers.seedsow.revision);
  assert.ok(!JSON.stringify(report).includes(api.lock.token));
  const two = await Promise.all([
    api.post('/api/operation', {action: 'export-sd', kind: 'app'}),
    api.post('/api/operation', {action: 'export-sd', kind: 'app'}),
  ]);
  assert.deepEqual(two.map(x => x.status).sort(), [202, 400]);
  await untilDone(api);
  const aborted = new AbortController();
  const disconnected = api.get('/api/state', {signal: aborted.signal});
  aborted.abort();
  await disconnected.catch(() => {});
  assert.equal((await api.get('/api/state')).status, 200,
    'a closed browser request does not stop the controller');
});

test('support copy redacts a home path emitted by a host tool', async t => {
  const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'moader-setup-tool-'));
  t.after(() => fsp.rm(dir, {recursive: true, force: true}));
  const home = path.posix.join('/', 'Users', 'fixture');
  await fsp.writeFile(path.join(dir, 'clang'),
    '#!/bin/sh\nprintf "clang version fixture\\nInstalledDir: ' +
      home + '/Library/Toolchains\\n"\n',
    {mode: 0o755});
  const api = await startController(t, null, {PATH: dir + path.delimiter + process.env.PATH});
  const report = await (await api.get('/api/report')).json();
  assert.match(report.system.clang, /\[home\]\/Library\/Toolchains/);
  assert.ok(!JSON.stringify(report).includes(home));
});

test('restart makes a held native session uncertain, blocks retry and retains evidence', async t => {
  const privateAddress = ['192', '168', '10', '7'].join('.');
  const run = await fsp.mkdtemp(path.join(os.tmpdir(), 'moader external run '));
  t.after(() => fsp.rm(run, {recursive: true, force: true}));
  await fsp.writeFile(path.join(run, 'moader-install.json'),
    JSON.stringify({status: 'stopped', address: privateAddress, token: 'fixture-secret'}));
  const seed = {schema: 1, prefs: {lastMethod: 'ns2jb', console: '', listenerPort: 31337},
    operation: {id: 'fixture', action: 'start-host', phase: 'waiting-for-user',
      message: 'Waiting', status: 'running', nativeStarted: true, run},
    nativeHistory: [{method: 'ns2jb-acquisition', status: 'started', bootEpoch: 1,
      at: new Date().toISOString()}],
    events: [{id: 1, at: new Date().toISOString(), type: 'log',
      message: 'address=' + privateAddress + ' token=fixture-secret'}], sequence: 1,
    prepared: {}, selectedCapture: null,
    selectedSession: {method: 'ns2jb', run},
    host: {method: 'ns2jb', status: 'held', run},
    sdPlan: null, sdResult: null, bootEpoch: 1, uncertain: null};
  const api = await startController(t, seed);
  let state = await (await api.get('/api/state')).json();
  assert.equal(state.operation.status, 'uncertain');
  assert.equal(state.host.status, 'ended');
  assert.equal(state.selectedSession, null);
  assert.ok(state.uncertain);
  const report = await (await api.get('/api/report')).json();
  assert.ok(report.journals.some(x => x.name === 'moader-install.json'));
  const copied = JSON.stringify(report);
  assert.ok(!copied.includes(privateAddress));
  assert.ok(!copied.includes('fixture-secret'));
  assert.equal((await api.post('/api/operation', {action: 'start-host', method: 'ns2jb',
    address: privateAddress})).status, 202);
  state = await untilDone(api);
  assert.equal(state.operation.status, 'failed');
  assert.match(state.operation.firstError, /prior console result is uncertain/i);
  assert.ok(state.uncertain, 'uncertainty is not cleared by a failed retry');
  assert.equal((await api.post('/api/new-boot', {confirmed: false})).status, 400);
  assert.equal((await api.post('/api/new-boot', {confirmed: true})).status, 200);
  state = await (await api.get('/api/state')).json();
  assert.equal(state.uncertain, null);
  assert.equal(state.bootEpoch, 2);
  assert.equal(state.bootAttempts.length, 0);
});

test('save upload failure is uncertain and cannot be replayed', async t => {
  const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'moader-setup-shim-'));
  t.after(() => fsp.rm(dir, {recursive: true, force: true}));
  const shim = path.join(dir, 'python-shim');
  await fsp.writeFile(shim, '#!/bin/sh\nif [ "$1" = "-c" ]; then echo 3.12; exit 0; fi\necho fixture-upload-failed >&2\nexit 7\n', {mode: 0o755});
  const seed = {schema: 1, prefs: {lastMethod: 'seedsow', console: '', listenerPort: 31337},
    operation: null, nativeHistory: [], events: [], sequence: 0, prepared: {},
    selectedCapture: {file: path.join(dir, 'fixture')}, standalonePayloadDir: dir,
    selectedSession: null, host: null, sdPlan: null, sdResult: null,
    bootEpoch: 1, uncertain: null};
  const api = await startController(t, seed, {MOADER_PYTHON: shim});
  assert.equal((await api.post('/api/operation', {action: 'upload-standalone',
    address: ['192', '168', '10', '8'].join('.'), profileConfirmed: true,
    backupConfirmed: true, edition: 'demo'})).status, 202);
  const done = await untilDone(api);
  assert.equal(done.operation.status, 'uncertain');
  assert.match(done.operation.firstError, /fixture-upload-failed/);
  assert.equal(done.bootAttempts.length, 1);
  assert.ok(done.uncertain);
  assert.equal((await api.post('/api/operation', {action: 'upload-standalone',
    address: ['192', '168', '10', '8'].join('.'), profileConfirmed: true,
    backupConfirmed: true, edition: 'demo'})).status, 202);
  const again = await untilDone(api);
  assert.equal(again.operation.status, 'failed');
  assert.equal(again.bootAttempts.length, 1);
});
