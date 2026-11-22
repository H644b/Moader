// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const http = require('node:http');
const crypto = require('node:crypto');
const {spawn, spawnSync} = require('node:child_process');
const hostPolicy = require('./host-policy.cjs');
const hostTools = require('./host-tools.cjs');

const root = path.resolve(__dirname, '..');
// The desktop window uses the same local identity assets as the paired browser.
const uiFiles = new Map([
  ['/', ['setup/ui/index.html', 'text/html; charset=utf-8']],
  ['/style.css', ['setup/ui/style.css', 'text/css; charset=utf-8']],
  ['/app.js', ['setup/ui/app.js', 'text/javascript; charset=utf-8']],
  ['/favicon.svg', ['branding/setup-icon.svg', 'image/svg+xml']],
  ['/lockup.svg', ['branding/lockup.svg', 'image/svg+xml']],
  ['/font/text.ttf', ['native/assets/Text.ttf', 'font/ttf']],
  ['/font/strong.ttf', ['native/assets/Strong.ttf', 'font/ttf']]
]);
const release = JSON.parse(fs.readFileSync(path.join(root, 'config/release.json')));
const stateDir = process.env.MOADER_SETUP_STATE_DIR
  ? path.resolve(process.env.MOADER_SETUP_STATE_DIR) : path.join(root, 'state/setup');
const stateFile = path.join(stateDir, 'controller-state.json');
const lockFile = path.join(stateDir, 'controller.json');
const platformKey = process.platform + '-' + process.arch;
const uvFile = path.join(root, 'host-runtimes/uv-v0.12.16', platformKey, 'uv');
const uvEnv = {UV_PYTHON_INSTALL_DIR: path.join(stateDir, 'python'),
  UV_CACHE_DIR: path.join(stateDir, 'uv-cache'), UV_PYTHON_INSTALL_BIN: '0',
  UV_NO_PROGRESS: '1'};
const guiDir = path.join(root, 'build/setup-seedsow-gui');
const payloadRoot = path.join(root, 'build');
const HOST_PROFILES = Object.freeze({
  seedsow: 'seedsow-malev0', mac2jb: 'smilebasic-kurumuz',
  ns2jb: 'smilebasic-kurumuz', civswitch: 'civ7-s8n',
});
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const fail = message => { throw Error(message); };
const requireTrue = (value, message) => { if (!value) fail(message); };
const now = () => new Date().toISOString();
const realFile = p => fs.existsSync(p) && fs.lstatSync(p).isFile() && !fs.lstatSync(p).isSymbolicLink();
const filename = p => path.basename(p || '');
const readJson = p => JSON.parse(fs.readFileSync(p, 'utf8'));
const writeJson = (p, value) => {
  fs.mkdirSync(path.dirname(p), {recursive: true, mode: 0o700});
  const tmp = p + '.' + process.pid + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(value, null, 2) + '\n', {mode: 0o600});
  fs.renameSync(tmp, p);
};
const privateIPv4 = value => {
  if (net.isIP(value) !== 4) return false;
  const n = value.split('.').map(Number);
  return n[0] === 10 || n[0] === 172 && n[1] >= 16 && n[1] <= 31 ||
    n[0] === 192 && n[1] === 168 || n[0] === 169 && n[1] === 254;
};
const cmdVersion = (exe, args = ['--version']) => {
  if (!exe) return '';
  const result = spawnSync(exe, args, {encoding: 'utf8', timeout: 3000, windowsHide: true});
  return result.status === 0 ? result.stdout.trim() : '';
};
const probePython = (exe, requireTrustStore) => {
  const result = spawnSync(exe, ['-c',
    'import sys,ssl; p=ssl.get_default_verify_paths(); print("%d.%d" % sys.version_info[:2]); print(p.cafile or ""); print(p.capath or "")'],
  {encoding: 'utf8', timeout: 3000});
  if (result.status !== 0) return null;
  const [version, caFile, caDirectory] = result.stdout.trim().split(/\r?\n/);
  const [major, minor] = version.split('.').map(Number);
  if (major !== 3 || minor < 10) return null;
  if (requireTrustStore && !((caFile && realFile(caFile)) ||
      (caDirectory && fs.existsSync(caDirectory) && fs.statSync(caDirectory).isDirectory())))
    return null;
  return {exe, version};
};
const findPython = () => {
  // An explicit override is an advanced-user choice. Automatic selection must
  // also be able to verify pinned HTTPS downloads on a first-run computer.
  if (process.env.MOADER_PYTHON) {
    const selected = probePython(process.env.MOADER_PYTHON, false);
    if (selected) return selected;
  }
  if (realFile(uvFile)) {
    const found = spawnSync(uvFile, ['--no-config', 'python', 'find', '--managed-python',
      '--no-python-downloads', '3.12.12'], {encoding: 'utf8', timeout: 5000,
      env: {...process.env, ...uvEnv}});
    const selected = found.status === 0 ? found.stdout.trim() : '';
    const qualified = selected && realFile(selected) ? probePython(selected, true) : null;
    if (qualified?.version === '3.12')
      return {exe: selected, version: '3.12.12 (kit scoped)'};
  }
  return probePython('python3', true);
};
const system = () => ({
  os: process.platform, arch: process.arch, wsl: process.platform === 'linux' &&
    /microsoft|wsl/i.test(os.release()), node: process.version, python: findPython()?.version || null,
  git: cmdVersion('git'), clang: cmdVersion('clang'), zstd: cmdVersion('zstd'),
  release: release.version,
});
const hostAddresses = () => [...new Set(Object.values(os.networkInterfaces()).flat()
  .filter(row => row && row.family === 'IPv4' && !row.internal &&
    privateIPv4(row.address)).map(row => row.address))];
function checkHostDependencies(method) {
  requireTrue(cmdVersion('git'), 'Git is needed to fetch the pinned provider. Open the operating system installer from Setup, then click Prepare and verify kit again.');
  if (method === 'seedsow') {
    requireTrue(cmdVersion('clang'), 'AArch64 source preparation needs clang. Open the operating system installer from Setup, then click Prepare and verify kit again.');
    if (process.platform === 'linux')
      requireTrue(cmdVersion('zstd'), 'The kit-scoped SDK needs zstd. Open your Linux software manager from Setup, then click Prepare and verify kit again.');
  }
}
function executableInPath(name) {
  for (const folder of (process.env.PATH || '').split(path.delimiter)) {
    const file = path.join(folder, name);
    try { if (fs.statSync(file).isFile()) { fs.accessSync(file, fs.constants.X_OK); return file; } }
    catch { /* Continue to the next path entry. */ }
  }
  return null;
}
function missingHostTools(method) {
  return [!cmdVersion('git') && 'git', method === 'seedsow' &&
    !cmdVersion('clang') && 'clang', method === 'seedsow' &&
    process.platform === 'linux' && !cmdVersion('zstd') && 'zstd'].filter(Boolean);
}
async function installHostTools(method) {
  requireTrue(process.platform === 'linux' && supported(method),
    'Computer tool installation is available for supported Linux setup methods.');
  const packages = missingHostTools(method);
  if (!packages.length) return;
  const manager = hostTools.managers.find(item => {
    try { return fs.statSync(item.path).isFile(); } catch { return false; }
  });
  const wsl = /microsoft|wsl/i.test(os.release());
  const steps = hostTools.plan({wsl, distro: process.env.WSL_DISTRO_NAME,
    wslExe: executableInPath('wsl.exe') ||
      (fs.existsSync('/mnt/c/Windows/System32/wsl.exe') ?
        '/mnt/c/Windows/System32/wsl.exe' : null),
    pkexec: executableInPath('pkexec'), manager, packages});
  for (const step of steps) await runStep(step.label, step.exe, step.args,
    {timeoutMs: 1200000});
  requireTrue(missingHostTools(method).length === 0,
    'The Linux package manager returned, but a required tool is still unavailable. Review its output and retry the preparation check.');
}
function openDependencyManager() {
  let executable, args;
  if (process.platform === 'darwin') {
    executable = '/usr/bin/xcode-select'; args = ['--install'];
  } else {
    executable = ['gnome-software', 'plasma-discover'].map(name =>
      (process.env.PATH || '').split(path.delimiter).map(folder =>
        path.join(folder, name)).find(file => {
          try { fs.accessSync(file, fs.constants.X_OK); return realFile(file); }
          catch { return false; }
        })).find(Boolean);
    args = [];
  }
  if (!executable) return Promise.resolve({opened: false,
    message: 'No graphical software manager was found in this Linux session. Open the distribution’s Software app, install the named tool, then choose Prepare and verify kit again.'});
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, {stdio: 'ignore', detached: true,
      windowsHide: true});
    child.once('error', reject);
    child.once('spawn', () => { child.unref(); resolve({opened: true,
      message: process.platform === 'darwin'
        ? 'The macOS Command Line Tools installer was opened. Finish its prompts, then choose Prepare and verify kit again.'
        : 'The Linux Software app was opened. Install the named tool, then choose Prepare and verify kit again.'}); });
  });
}
const supported = method => {
  return hostPolicy.supported(method, process.platform, process.arch);
};
const checkKit = () => {
  const files = Object.values(release.binaries).concat([
    release.patched_hen, release.patched_ro, release.patched_bootstrap]);
  const missing = [], wrong = [];
  for (const item of files) {
    const p = path.join(root, item.source);
    if (!realFile(p)) missing.push(item.source);
    else if (fs.statSync(p).size !== item.bytes || sha(fs.readFileSync(p)) !== item.sha256)
      wrong.push(item.source);
  }
  return {ready: !missing.length && !wrong.length, missing, wrong,
    version: release.version, componentHashes: Object.fromEntries(files.map(x => [x.source, x.sha256]))};
};
fs.mkdirSync(stateDir, {recursive: true, mode: 0o700});
let state = fs.existsSync(stateFile) ? readJson(stateFile) : {
  schema: 1, prefs: {lastMethod: 'seedsow', console: '', listenerPort: 31337},
  operation: null, nativeHistory: [], events: [], sequence: 0, prepared: {}, selectedCapture: null,
  selectedSession: null, host: null, sdPlan: null, sdResult: null,
  bootEpoch: 1, uncertain: null, launcher: null, standalonePayloadDir: null,
};
// A reply from a previous controller lifetime is never a current connection check.
state.launcher = null;
if (state.operation?.status === 'running' || state.operation?.status === 'waiting') {
  const native = state.operation.nativeStarted === true;
  state.operation.status = native ? 'uncertain' : 'failed';
  state.operation.phase = native ? 'outcome-uncertain' : 'failed-before-device';
  state.operation.message = native
    ? 'The setup controller restarted during a console operation. Keep the original session and reports. Reboot the Switch before another native attempt.'
    : 'Computer preparation stopped when the controller closed. You may retry this host-only step.';
  state.operation.firstError ||= 'Controller restarted before the operation result was verified.';
  if (native) state.uncertain = {at: now(), method: state.operation.action,
    run: state.operation.run, reason: state.operation.firstError};
}
// A provider process cannot be recovered by reopening the controller.
if (state.host?.status === 'held' || state.host?.status === 'starting') {
  state.uncertain ||= {at: now(), method: state.host.method + '-acquisition',
    run: state.host.run,
    reason: 'The controller restarted and no longer owns the held provider process. Keep its report and reboot before another native action.'};
  state.host.status = 'ended';
  state.selectedSession = null;
}
let active = false;
let hostChild = null;
let activeChild = null;
let holdSleep = null;
function save() {
  state.events = state.events.slice(-600);
  writeJson(stateFile, state);
}
function event(type, message, details = {}) {
  const row = {id: ++state.sequence, at: now(), type, message: String(message).slice(0, 1000), ...details};
  state.events.push(row);
  save();
  return row;
}
function operation(action, phase, message) {
  state.operation = {id: crypto.randomUUID(), action, phase, message, status: 'running',
    started: now(), finished: null, firstError: null, run: null, nativeStarted: false};
  event('stage', message, {phase});
}
function phase(value, message) {
  state.operation.phase = value;
  state.operation.message = message;
  event('stage', message, {phase: value});
}
function finish(status, message, error = null) {
  state.operation.status = status;
  state.operation.phase = status === 'complete' ? 'completed' :
    status === 'uncertain' ? 'outcome-uncertain' : 'failed-before-device';
  state.operation.message = message;
  state.operation.finished = now();
  state.operation.firstError = error ? String(error.message || error).slice(0, 1000) : null;
  if (status === 'uncertain') state.uncertain = {at: now(),
    method: state.operation.action, run: state.operation.run,
    reason: state.operation.firstError};
  event(status, message, {phase: state.operation.phase});
}
function nativeLock(method, run = null) {
  requireTrue(!state.uncertain, 'A console operation has an uncertain result. Keep its evidence and complete a full Switch reboot before another native action.');
  const prior = state.nativeHistory.filter(x => x.status === 'started' &&
    x.bootEpoch === (state.bootEpoch || 1));
  const continuedHeld = prior.length === 1 &&
    prior[0].method === method + '-acquisition' &&
    state.host?.method === method && state.host.status === 'held';
  requireTrue(prior.length === 0 || continuedHeld,
    'A native attempt is already recorded for this boot. Complete a full Switch reboot before another attempt.');
  const record = {id: crypto.randomUUID(), method, at: now(), status: 'started', run,
    operationId: state.operation.id, bootEpoch: state.bootEpoch || 1};
  state.nativeHistory.push(record);
  state.operation.nativeStarted = true;
  event('native-started', 'One console attempt has been recorded.', {method});
  return record;
}
function checkNativeAttemptBeforeContact(action, input) {
  requireTrue(!state.uncertain,
    'A prior console result is uncertain. Keep its evidence and complete a full Switch reboot before another native action.');
  const prior = state.nativeHistory.filter(x => x.status === 'started' &&
    x.bootEpoch === (state.bootEpoch || 1));
  const continuation = action === 'install-held' &&
    prior.length === 1 && prior[0].method === input.method + '-acquisition' &&
    state.host?.method === input.method && state.host.status === 'held' &&
    hostChild && hostChild.exitCode === null;
  requireTrue(!prior.length || continuation,
    'A native attempt is already recorded for this boot. Complete a full Switch reboot before another attempt.');
}
async function closeBoot(confirmed) {
  requireTrue(confirmed === true, 'Confirm that the Switch was fully rebooted.');
  requireTrue(!active, 'A setup operation is still active.');
  const current = state.nativeHistory.filter(x => x.status === 'started' &&
    x.bootEpoch === (state.bootEpoch || 1));
  requireTrue(current.length > 0, 'No current-boot attempt needs closing.');
  if (hostChild) {
    const child = hostChild;
    child.kill('SIGTERM');
    await new Promise(resolve => {
      if (child.exitCode !== null) return resolve();
      const timer = setTimeout(() => { child.kill('SIGKILL'); resolve(); }, 5000);
      child.once('exit', () => { clearTimeout(timer); resolve(); });
    });
    requireTrue(hostChild === null || child.exitCode !== null,
      'The old provider host has not stopped. Keep its report and wait.');
  }
  for (const row of current) { row.status = 'closed-boot'; row.closedAt = now(); }
  state.bootEpoch = (state.bootEpoch || 1) + 1;
  state.uncertain = null;
  state.selectedSession = null;
  state.host = null;
  state.launcher = null;
  event('boot', 'A full Switch reboot was confirmed. Prior attempt records were kept.',
    {bootEpoch: state.bootEpoch});
}
function logLine(source, line) {
  if (!line) return;
  event('log', line.slice(0, 1800), {source});
  const match = /^MOADER_RUN (.+)$/.exec(line);
  if (match && state.operation) {
    const run = path.resolve(match[1]);
    if (run.startsWith(path.join(root, 'build/seedsow-runs') + path.sep) &&
      /^switch-[A-Za-z0-9]+$/.test(filename(run)) && fs.existsSync(run) &&
      fs.realpathSync(run) === run) {
      state.operation.run = run;
      save();
    }
  }
}
function preventSleep(child) {
  if (process.platform === 'darwin' && fs.existsSync('/usr/bin/caffeinate')) {
    holdSleep = spawn('/usr/bin/caffeinate', ['-i', '-w', String(child.pid)],
      {stdio: 'ignore', windowsHide: true});
  }
}
function watchInstallJournal(run) {
  if (!run || !state.operation) return;
  const file = path.join(run, 'moader-install.json');
  if (!realFile(file) || fs.statSync(file).size > 256000) return;
  try {
    const status = readJson(file).status;
    const messages = {
      'installing-filesystem': 'The installer is writing the matched filesystem runtime.',
      'starting-kaihen': 'The filesystem step finished; the installer is starting the matched backend.',
      'awaiting-device-tests': 'The backend installer finished; checking its final service and owner receipts.',
      stopped: 'The installer recorded a stop. Keep its journal for recovery.',
    };
    if (messages[status] && state.operation.lastJournalStatus !== status) {
      state.operation.lastJournalStatus = status;
      phase(status === 'awaiting-device-tests' ? 'verifying' : 'installing', messages[status]);
    }
  } catch { /* A journal can be between writes; the next poll will read it. */ }
}
function command(exe, args, {label, native = false, env = {}, timeoutMs = 600000, cwd = root} = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, args, {cwd, env: {...process.env, ...env}, windowsHide: true,
      stdio: ['ignore', 'pipe', 'pipe']});
    activeChild = child;
    if (native) preventSleep(child);
    let output = '', last = '', total = 0, timer = null;
    const progress = native ? setInterval(() => watchInstallJournal(state.operation?.run), 1200) : null;
    const consume = (stream, source) => {
      let pending = '';
      stream.on('data', bytes => {
        total += bytes.length;
        if (total > 4 * 1024 * 1024) {
          logLine(source, 'Output limit reached; full native outcome is uncertain.');
          return;
        }
        pending += bytes.toString('utf8');
        if (pending.length > 8192) pending = pending.slice(-8192);
        const lines = pending.split(/\r?\n/);
        pending = lines.pop();
        for (const line of lines) {
          if (source === 'stdout') output += line + '\n';
          last = line;
          logLine(label || source, line);
        }
      });
      stream.on('end', () => { if (pending) logLine(label || source, pending); });
    };
    consume(child.stdout, 'stdout');
    consume(child.stderr, 'stderr');
    child.on('error', error => {
      if (timer) clearTimeout(timer);
      if (progress) clearInterval(progress);
      reject(error);
    });
    child.on('exit', (code, signal) => {
      if (timer) clearTimeout(timer);
      if (progress) { clearInterval(progress); watchInstallJournal(state.operation?.run); }
      activeChild = null;
      holdSleep = null;
      if (code === 0 && !signal && total <= 4 * 1024 * 1024) resolve({output, last});
      else reject(Error((label || filename(exe)) + ' stopped (' + (signal || code) + '). ' + last));
    });
    if (!native && timeoutMs) timer = setTimeout(() => {
      child.kill('SIGTERM');
      reject(Error((label || filename(exe)) + ' timed out during computer preparation.'));
    }, timeoutMs);
  });
}
function python() {
  const found = findPython();
  requireTrue(found, 'Python 3.10 or newer could not be prepared for this kit.');
  return found.exe;
}
async function ensurePython() {
  if (findPython()) return;
  requireTrue(realFile(uvFile), 'The included Python preparation helper is missing. Extract the complete backend kit again.');
  fs.chmodSync(uvFile, 0o755);
  phase('downloading', 'Obtaining a kit-scoped Python 3.12.12 from the pinned managed distribution.');
  await command(uvFile, ['--no-config', 'python', 'install', '3.12.12'],
    {label: 'Kit-scoped Python preparation', env: uvEnv, timeoutMs: 1200000});
  requireTrue(findPython(), 'The downloaded Python did not pass its version and executable check.');
}
function node24() {
  requireTrue(/^v24\./.test(process.version) && ['darwin', 'linux'].includes(process.platform),
    'The setup controller needs native macOS or Linux Node.js 24.');
  return process.execPath;
}
async function runStep(name, exe, args, opts = {}) {
  phase('preparing', name);
  return command(exe, args, {label: name, ...opts});
}
async function prepare(method) {
  requireTrue(supported(method), 'This setup method is unavailable on the detected host.');
  requireTrue(checkKit().ready, 'The extracted backend kit has missing or mismatched components.');
  checkHostDependencies(method);
  const py = python(), node = node24(), profile = HOST_PROFILES[method];
  if (method === 'seedsow') await runStep('Preparing the kit-scoped SDK', py,
    [path.join(root, 'tools/setup-devkit.py')], {timeoutMs: 1200000});
  await runStep('Checking pinned backend sources and matched files', py,
    [path.join(root, 'tools/setup-backend.py'), '--host-profile', profile, '--node', node],
    {timeoutMs: 1200000});
  await runStep('Verifying prepared backend inputs', py,
    [path.join(root, 'tools/setup-backend.py'), '--check', '--host-profile', profile, '--node', node]);
  if (method === 'seedsow') {
    fs.mkdirSync(path.dirname(guiDir), {recursive: true, mode: 0o700});
    const qualified = spawnSync(node, [path.join(root, 'tools/prepare-seedsow-gui.cjs'), '--check', guiDir],
      {cwd: root, encoding: 'utf8', timeout: 10000});
    if (qualified.status !== 0) {
      requireTrue(!fs.existsSync(guiDir), 'The existing Seedsow GUI source differs from the reviewed patch. Keep it for inspection; choose a clean kit folder.');
      await runStep('Provisioning the reviewed Seedsow provider', node,
        [path.join(root, 'tools/prepare-seedsow-gui.cjs'), '--provision', guiDir],
        {timeoutMs: 240000});
    }
    await runStep('Staging the exact matched Moader bundle', node,
      [path.join(root, 'setup/bridge.cjs'), 'stage']);
    await runStep('Checking the staged bundle', node, [path.join(root, 'setup/bridge.cjs'), 'check']);
  }
  state.prepared[method] = {at: now(), profile, version: release.version};
  save();
}
function checkLauncher(address) {
  return new Promise((resolve, reject) => {
    requireTrue(privateIPv4(address), 'Enter the Switch’s current private LAN IPv4 address.');
    const request = http.get({hostname: address, port: 8080, path: '/status', timeout: 4000},
      response => {
        let data = '';
        response.on('data', bytes => {
          data += bytes.toString('utf8');
          if (data.length > 8192) request.destroy(Error('Launcher status reply was too large.'));
        });
        response.on('end', () => response.statusCode === 200 && /^status: ok(?:\r?\n|$)/.test(data)
          ? resolve({online: true, message: 'Seedsow launcher answered on this address. FS access is not verified yet.'})
          : reject(Error('Seedsow launcher did not return its expected status. Open the matching edition and press Y.')));
      });
    request.on('timeout', () => request.destroy(Error('Waiting for the Seedsow launcher timed out. Check the current Switch address and press Y.')));
    request.on('error', reject);
  });
}
function verifySeedsowResult(run) {
  requireTrue(run && realFile(path.join(run, 'moader-success-retirement.json')),
    'The final Seedsow retirement receipt is missing.');
  const retired = readJson(path.join(run, 'moader-success-retirement.json'));
  const install = readJson(path.join(run, 'moader-install.json'));
  requireTrue(install.complete === true && retired.run === run &&
    retired.probe?.cacheRestored === true && retired.probe.endpointClosed === true &&
    retired.listener?.tcpClosed === true && retired.listener.serveEnded === true &&
    retired.hostOwnerExiting === true && realFile(path.join(run, 'moader-device-verification-pending.json')),
  'The Seedsow installer did not prove its final service check and owner retirement.');
}
async function installSeedsow(address, edition) {
  requireTrue(['demo', 'full'].includes(edition), 'Choose the Seedsow edition on the Switch.');
  state.operation.edition = edition;
  event('choice', 'The user selected the Seedsow ' + edition +
    ' edition. The launcher protocol cannot prove the active game or profile.');
  requireTrue(state.prepared.seedsow?.version === release.version,
    'Prepare and verify the Seedsow host kit first.');
  await checkLauncher(address);
  const preflightDir = path.join(root, 'build', 'setup-preflight-' + crypto.randomBytes(6).toString('hex'));
  phase('checking', 'Checking this Seedsow launcher before installation.');
  await command(python(), [path.join(root, 'tools/seedsow-preflight.py'), '--console', address,
    '--output', preflightDir], {label: 'Seedsow read-only preflight'});
  const report = readJson(path.join(preflightDir, 'preflight.json'));
  requireTrue(report.complete === true && report.allRequiredPinnedSignaturesMatch === true &&
    report.fsAcquisitionAttempted === false && report.codeOrKernelPatchAttempted === false,
  'The current Seedsow launcher did not pass the pinned read-only preflight.');
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) =>
    !key.startsWith('NS2_') && !key.startsWith('KAIHEN_')));
  env.MOADER_AARCH64_OBJCOPY = path.join(root, 'vendor/devkitpro/devkitA64/bin/aarch64-none-elf-objcopy');
  const clang = process.platform === 'darwin'
    ? spawnSync('xcrun', ['--find', 'clang'], {encoding: 'utf8'}).stdout?.trim() : 'clang';
  requireTrue(clang, 'AArch64 clang is missing; complete the host preparation handoff first.');
  env.MOADER_AARCH64_CLANG = clang;
  nativeLock('seedsow');
  phase('installing', 'Installing the backend for this boot. Keep Seedsow and this computer awake.');
  const result = await command(node24(), [path.join(root, 'tools/seedsow-install.cjs'),
    '--start', '--console', address, '--preflight', path.join(preflightDir, 'preflight.json'),
    '--upstream-env', path.join(root, 'research/sources/seedsow-fs-kaihen/env.txt')],
  {label: 'Seedsow installer', native: true, env});
  phase('verifying', 'Checking the final Seedsow receipt and listener closure.');
  verifySeedsowResult(state.operation.run);
  return result;
}
async function prepareHeld(method, input) {
  requireTrue(['mac2jb', 'ns2jb', 'civswitch'].includes(method), 'Invalid held-session method.');
  requireTrue(state.prepared[method]?.version === release.version, 'Prepare this method’s host kit first.');
  const args = [path.join(root, 'tools/install-from-rpc.py'), '--prepare', '--json'];
  if (method === 'mac2jb') {
    requireTrue(typeof input.statusCommand === 'string' && input.statusCommand.length <= 4096,
      'Paste the complete current mac2jb status command.');
    args.push('--status-command', input.statusCommand);
  } else {
    const run = method === 'ns2jb' || method === 'civswitch'
      ? input.run || state.host?.run : null;
    if (state.host?.method === method)
      requireTrue(state.host.status === 'held' && hostChild &&
        hostChild.exitCode === null, 'The supervised provider is no longer holding this session.');
    requireTrue(typeof run === 'string' && run.length <= 1024,
      'Select a verified held run from this boot.');
    args.push('--run-dir', run, '--node', node24());
    if (method === 'civswitch') args.push('--host-profile', HOST_PROFILES.civswitch);
  }
  phase('preparing', 'Qualifying the current held session and making a unique installer snapshot.');
  const result = await command(python(), args, {label: 'Held-session preparation', timeoutMs: 1200000});
  const lines = result.output.trim().split(/\r?\n/);
  const parsed = JSON.parse(lines.at(-1));
  requireTrue(parsed.prepared === true && parsed.schema === 1 && Array.isArray(parsed.command) &&
    parsed.command.length === 5 && parsed.command[2] === '--prepared' &&
    realFile(parsed.manifest), 'The installer did not return one qualified snapshot.');
  state.selectedSession = {method, run: parsed.run, node: parsed.node,
    manifest: parsed.manifest, command: parsed.command, at: now()};
  save();
}
function providerRuns(directory, since) {
  const candidates = [path.join(directory, 'runs'), path.join(path.dirname(directory), 'runs')];
  const found = [];
  for (const folder of candidates) {
    if (!fs.existsSync(folder)) continue;
    for (const entry of fs.readdirSync(folder, {withFileTypes: true})) {
      if (!entry.isDirectory() || !/^switch2-[A-Za-z0-9]+$/.test(entry.name)) continue;
      const run = path.join(folder, entry.name);
      if (fs.lstatSync(run).birthtimeMs < since || fs.realpathSync(run) !== run) continue;
      try {
        require(path.join(root, 'runtime/modloader/install.cjs')).verifiedRun(run);
        found.push(run);
      } catch {}
    }
  }
  return found;
}
async function startHost(method, address) {
  requireTrue(['ns2jb', 'civswitch'].includes(method) && supported(method),
    'Choose direct ns2jb or experimental CivSwitch on a supported host.');
  requireTrue(state.prepared[method]?.version === release.version,
    'Prepare this method’s pinned sources first.');
  requireTrue(privateIPv4(address), 'Enter a private LAN IPv4 address.');
  if (method === 'ns2jb') {
    await new Promise((resolve, reject) => {
      const server = net.createServer();
      server.once('error', () => reject(Error('The chosen computer address cannot bind TCP 39380. Select a local LAN address visible inside this OS or WSL distro.')));
      server.listen(39380, address, () => server.close(resolve));
    });
  }
  const directory = method === 'ns2jb'
    ? path.join(root, 'research/sources/ns2jb/engine')
    : path.join(root, 'research/sources/civ7-fs-kaihen/engine');
  const runner = path.join(directory, 'run.cjs');
  requireTrue(realFile(runner), 'The pinned provider runner was not prepared.');
  nativeLock(method + '-acquisition');
  phase('waiting-for-user', method === 'ns2jb'
    ? 'Waiting for SmileBASIC and the verified held FS session. Follow the provider prompts on the Switch; 4/4 verification and a qualified run folder mean success.'
    : 'Waiting for the CivSwitch loader and a verified held FS-only session. Follow the console steps; the loader waiting screen alone is not success.');
  state.host = {method, address, status: 'starting', started: now(), run: null};
  save();
  const started = Date.now();
  const args = method === 'ns2jb' ? [runner, '--rpc', address] : [runner, address];
  const child = spawn(node24(), args, {cwd: directory, env: process.env,
    stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true});
  hostChild = child;
  preventSleep(child);
  let settled = false;
  const consume = stream => {
    let pending = '';
    stream.on('data', bytes => {
      pending += bytes.toString('utf8');
      if (pending.length > 8192) pending = pending.slice(-8192);
      const rows = pending.split(/\r?\n/);
      pending = rows.pop();
      rows.forEach(row => logLine(method + ' provider', row));
    });
  };
  consume(child.stdout);
  consume(child.stderr);
  return new Promise((resolve, reject) => {
    const poll = setInterval(() => {
      if (settled) return;
      const runs = providerRuns(directory, started);
      if (!runs.length) return;
      settled = true;
      clearInterval(poll);
      state.host.run = runs.at(-1);
      state.host.status = 'held';
      event('held', 'The provider has a verified held run from this controller session.', {method});
      resolve();
    }, 2000);
    child.once('error', error => {
      if (settled) return;
      settled = true; clearInterval(poll); reject(error);
    });
    child.once('exit', (code, signal) => {
      hostChild = null;
      holdSleep = null;
      state.host.status = 'ended';
      event('host-ended', 'The provider host ended (' + (signal || code) + '). Keep its report.', {method});
      if (!settled) {
        settled = true; clearInterval(poll);
        reject(Error('The provider stopped before a verified held run was observed.'));
      }
    });
  });
}
async function installHeld(method) {
  const selected = state.selectedSession;
  requireTrue(selected?.method === method && realFile(selected.manifest),
    'Prepare the current held session first.');
  if (state.host?.method === method)
    requireTrue(state.host.status === 'held' && hostChild &&
      hostChild.exitCode === null, 'The supervised provider stopped. Do not use its old run.');
  nativeLock(method, selected.run);
  state.operation.run = selected.run;
  phase('installing', 'Installing the filesystem runtime and starting the matched backend. Keep the game and host open.');
  await command(selected.command[0], selected.command.slice(1),
    {label: 'Moader combined installer', native: true});
  phase('verifying', 'Checking the installation journal.');
  const journal = readJson(path.join(selected.run, 'moader-install.json'));
  requireTrue(journal.complete === true && journal.status === 'awaiting-device-tests',
    'The combined installer did not record completion.');
}
async function generateStandalone(capture) {
  const item = qualifyCapture(capture || state.selectedCapture?.file);
  const payloadDir = path.join(payloadRoot, 'setup-standalone-payload-' +
    crypto.randomBytes(6).toString('hex'));
  phase('preparing', 'Generating the standalone FS payload from the qualified local capture.');
  await command(node24(), [path.join(root, 'tools/seedsow-standalone-fs-payload.cjs'),
    '--capture', item.file, '--output', payloadDir], {label: 'Standalone payload generator', timeoutMs: 1200000});
  phase('preparing', 'Checking pinned standalone save modules and staged files.');
  await command(python(), [path.join(root, 'tools/seedsow-standalone.py'), 'provision',
    '--node', node24()], {label: 'Standalone source provision', timeoutMs: 240000});
  await command(python(), [path.join(root, 'tools/seedsow-standalone.py'), 'check',
    '--payload-dir', payloadDir, '--node', node24()], {label: 'Standalone input check'});
  state.selectedCapture = {file: item.file, at: now(), sha256: item.sha256};
  state.standalonePayloadDir = payloadDir;
  save();
}
async function exportSd(kind) {
  requireTrue(['app', 'standalone'].includes(kind), 'Choose an SD staging task.');
  requireTrue(checkKit().ready, 'The kit’s app and resident do not match this release.');
  if (kind === 'standalone') requireTrue(state.selectedCapture &&
    state.standalonePayloadDir && fs.existsSync(state.standalonePayloadDir),
    'Generate and check the standalone payload first.');
  const output = path.join(root, 'build/setup-sd-export-' + crypto.randomBytes(6).toString('hex'));
  const args = [path.join(root, 'setup/export-sd.py'), kind, '--output', output];
  if (kind === 'standalone') args.push('--payload-dir', state.standalonePayloadDir);
  phase('preparing', 'Building the checked file list for the SD card.');
  await command(python(), args, {label: 'SD file export', timeoutMs: 1200000});
  const plan = readJson(path.join(output, 'setup-export.json'));
  requireTrue(plan.schema === 1 && plan.kind === kind && plan.files.length > 0,
    'The SD file plan is incomplete.');
  state.sdPlan = {id: crypto.randomUUID(), kind, output, files: plan.files,
    totalBytes: plan.totalBytes, at: now()};
  state.sdResult = null;
  save();
}
const CAPTURE_SHA = '0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271';
function qualifyCapture(file) {
  requireTrue(typeof file === 'string' && file.length <= 2048 && realFile(file) &&
    filename(file) === 'fs-4.bin', 'Select the saved fs-4.bin capture from a completed PC-driven run.');
  const bytes = fs.readFileSync(file);
  const manifestFile = path.join(path.dirname(file), 'manifest.json');
  requireTrue(realFile(manifestFile), 'The adjacent capture qualification manifest is missing.');
  const manifest = readJson(manifestFile), digest = sha(bytes);
  requireTrue(bytes.length === 2416640 && digest === CAPTURE_SHA &&
    manifest.session === filename(path.dirname(path.dirname(file))) &&
    manifest.verified === true && manifest.validatedRanges === 36 &&
    manifest.runtimeLoggingCompatible === true && manifest.sha256 === digest,
  'This capture is not accepted by the current standalone A generator. Use the PC-driven method.');
  const run = path.join(root, 'build/seedsow-runs', manifest.session);
  const imported = path.join(stateDir, 'imported', manifest.session);
  const evidence = realFile(path.join(run, 'moader-success-retirement.json')) ? run : imported;
  requireTrue(realFile(path.join(evidence, 'moader-success-retirement.json')) &&
    realFile(path.join(evidence, 'moader-device-verification-pending.json')) &&
    realFile(path.join(evidence, 'moader-install.json')),
  'This capture has no completed PC-driven setup receipt. File presence alone is not enough.');
  const retirement = readJson(path.join(evidence, 'moader-success-retirement.json'));
  const install = readJson(path.join(evidence, 'moader-install.json'));
  requireTrue(filename(retirement.run) === manifest.session &&
    retirement.probe?.cacheRestored === true && retirement.probe.endpointClosed === true &&
    retirement.listener?.tcpClosed === true && retirement.listener.serveEnded === true &&
    install.complete === true, 'The PC-driven setup receipts do not prove completion.');
  return {file, sha256: digest, manifest};
}
function captureCandidates() {
  const folder = path.join(root, 'state');
  if (!fs.existsSync(folder)) return [];
  const current = fs.readdirSync(folder, {withFileTypes: true}).filter(x =>
    x.isDirectory() && /^switch-[A-Za-z0-9]+$/.test(x.name)).map(x =>
    path.join(folder, x.name, 'native-fs/fs-4.bin'));
  const imported = path.join(stateDir, 'imported');
  if (fs.existsSync(imported)) current.push(...fs.readdirSync(imported, {withFileTypes: true})
    .filter(x => x.isDirectory() && /^switch-[A-Za-z0-9]+$/.test(x.name))
    .map(x => path.join(imported, x.name, 'native-fs/fs-4.bin')));
  return current.filter(realFile).map(file => {
      try { return {...qualifyCapture(file), eligible: true}; }
      catch (error) { return {file, eligible: false, reason: error.message}; }
    });
}
function importCapture(files) {
  const names = ['fs-4.bin', 'manifest.json', 'moader-success-retirement.json',
    'moader-device-verification-pending.json', 'moader-install.json'];
  requireTrue(files && typeof files === 'object' &&
    Object.keys(files).sort().join('|') === names.sort().join('|'),
  'Select the capture, adjacent manifest and three completed run receipts.');
  const decoded = {};
  for (const name of names) {
    requireTrue(typeof files[name] === 'string' && files[name].length <=
      (name === 'fs-4.bin' ? 3300000 : 350000), 'An imported capture file is too large.');
    decoded[name] = Buffer.from(files[name], 'base64');
  }
  requireTrue(sha(decoded['fs-4.bin']) === CAPTURE_SHA,
    'This is not the exact accepted sparse FS capture.');
  const manifest = JSON.parse(decoded['manifest.json'].toString('utf8'));
  requireTrue(/^switch-[A-Za-z0-9]+$/.test(manifest.session),
    'The capture manifest has no valid session name.');
  const folder = path.join(stateDir, 'imported', manifest.session);
  requireTrue(!fs.existsSync(folder), 'This capture was already imported.');
  fs.mkdirSync(path.join(folder, 'native-fs'), {recursive: true, mode: 0o700});
  for (const name of names) fs.writeFileSync(path.join(folder,
    ['fs-4.bin', 'manifest.json'].includes(name) ? 'native-fs/' + name : name),
  decoded[name], {mode: 0o600});
  try { return qualifyCapture(path.join(folder, 'native-fs/fs-4.bin')); }
  catch (error) { fs.rmSync(folder, {recursive: true, force: true}); throw error; }
}
async function uploadStandalone(address, edition, profileConfirmed, backupConfirmed) {
  requireTrue(['demo', 'full'].includes(edition), 'Choose the active Seedsow edition.');
  requireTrue(profileConfirmed === true && backupConfirmed === true,
    'Confirm the active Seedsow profile and a separate backup before writing its save.');
  requireTrue(state.selectedCapture && state.standalonePayloadDir &&
    fs.existsSync(state.standalonePayloadDir),
    'Generate and check the standalone payload first.');
  requireTrue(privateIPv4(address), 'Enter the Switch’s current private LAN IPv4 address.');
  state.operation.edition = edition;
  event('choice', 'The user confirmed the ' + edition +
    ' edition and active profile before save upload; the address does not identify the profile.');
  nativeLock('standalone-save');
  phase('installing', 'Writing the standalone modules to the confirmed active Seedsow profile.');
  const result = await command(python(), [path.join(root, 'tools/seedsow-standalone.py'),
    'upload-save', '--host', address, '--payload-dir', state.standalonePayloadDir,
    '--node', node24()],
  {label: 'Standalone save upload', native: true});
  requireTrue(/commit_rc=0x0/.test(result.output), 'The save commit was not confirmed.');
}
async function verifyStandaloneSave(address) {
  requireTrue(privateIPv4(address), 'Enter the Switch’s current private LAN IPv4 address.');
  phase('verifying', 'Checking saved module presence and size on the current profile.');
  await command(python(), [path.join(root, 'tools/seedsow-standalone.py'),
    'verify-save', '--host', address, '--node', node24()],
  {label: 'Standalone save verification'});
}
function methodMessage(method) {
  if (method === 'install-host-tools') return 'The missing computer tools are available. Choose Prepare and verify kit to check the pinned provider and matched files. Nothing has been installed on the Switch.';
  if (method === 'seedsow') return 'The backend and service check completed for this boot. Press HOME, close Seedsow, then use hold R and open Moader from hbmenu. Repeat backend setup after a full reboot.';
  if (method === 'standalone-save') return 'The save upload committed. Fully reboot with the confirmed profile, press Y, verify the saved modules, then close and reopen Seedsow before choosing A or B.';
  if (method === 'standalone-verify') return 'The selected profile has the expected save files. Close and reopen Seedsow, then choose A for Moader FS or B for the upstream path on a fresh boot.';
  if (method === 'standalone') return 'The standalone payload is prepared on this computer. Choose the mounted SD card to stage and verify it, then upload the save on the confirmed profile.';
  if (method === 'mac2jb' || method === 'ns2jb') return 'Moader setup completed for this boot. Keep the host open until this result, then press HOME, close SmileBASIC, enter hbmenu with hold R and open Moader. Repeat backend setup after a full reboot.';
  if (method === 'civswitch') return 'The host installer completed. Close Civilization VII only after the original host returns. Hold R to enter hbmenu and check Moader on the Switch; this experimental handoff still needs hardware confirmation.';
  if (method === 'start-host') return 'The pinned provider has a verified held FS run. Keep its host and the game open, then prepare and run the Moader installer once.';
  return 'Computer preparation completed. Nothing has been installed on the Switch yet.';
}
async function runOperation(action, input) {
  requireTrue(!active, 'Another setup operation is already running.');
  active = true;
  operation(action, 'checking', 'Checking the selected setup task.');
  try {
    if (['seedsow-install', 'start-host', 'install-held', 'upload-standalone'].includes(action))
      checkNativeAttemptBeforeContact(action, input);
    if (['prepare', 'prepare-held', 'generate-standalone', 'export-sd',
      'upload-standalone', 'verify-standalone-save', 'seedsow-install'].includes(action))
      await ensurePython();
    let result;
    if (action === 'prepare') result = await prepare(input.method);
    else if (action === 'install-host-tools') result = await installHostTools(input.method);
    else if (action === 'start-host') result = await startHost(input.method, input.address);
    else if (action === 'seedsow-install') result = await installSeedsow(input.address,
      input.edition);
    else if (action === 'prepare-held') result = await prepareHeld(input.method, input);
    else if (action === 'install-held') result = await installHeld(input.method);
    else if (action === 'generate-standalone') result = await generateStandalone(input.capture);
    else if (action === 'export-sd') result = await exportSd(input.kind);
    else if (action === 'upload-standalone') result = await uploadStandalone(input.address,
      input.edition,
      input.profileConfirmed, input.backupConfirmed);
    else if (action === 'verify-standalone-save') result = await verifyStandaloneSave(input.address);
    else fail('Unknown setup operation.');
    finish('complete', methodMessage(action === 'seedsow-install' ? 'seedsow' :
      action === 'install-held' ? input.method : action === 'upload-standalone' ? 'standalone-save' :
      action === 'verify-standalone-save' ? 'standalone-verify' :
      action === 'generate-standalone' ? 'standalone' :
      action === 'start-host' ? 'start-host' :
      action === 'install-host-tools' ? 'install-host-tools' : 'prepare'));
    return result;
  } catch (error) {
    const native = state.operation.nativeStarted;
    finish(native ? 'uncertain' : 'failed', native
      ? 'The console operation did not return a verified completion. Keep the original session and report. Do not repeat it on this boot.'
      : 'Computer preparation stopped before a console installation. Correct the issue shown in details and retry this host step.',
    error);
  } finally { active = false; }
}

function redact(value) {
  return String(value)
    .replace(/\b(?:10|127|192)\.(?:\d{1,3}\.){2}\d{1,3}\b|\b172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}\b/g, '[private address]')
    .replace(/\/mnt\/[a-z]\/Users\/[^/\s"'<>]+/gi, '[home]')
    .replace(/\b[A-Za-z]:\\Users\\[^\\\s"'<>]+/g, '[home]')
    .replace(/\/(?:Users|home)\/[^/\s"'<>]+/g, '[home]')
    .replace(/(["']?)(?:pairing|token|password|secret|key)\1\s*[:=]\s*(["']?)[A-Za-z0-9._-]{6,}\2/gi,
      '[private value]')
    .replace(/\b\d{6}\b/g, '[six digits]');
}
function supportReport() {
  const op = state.operation;
  const journals = [];
  const reportRun = op?.run || state.uncertain?.run;
  let runReadable = false;
  try { runReadable = typeof reportRun === 'string' && path.isAbsolute(reportRun) &&
    fs.lstatSync(reportRun).isDirectory(); } catch { /* Keep the rest of the report. */ }
  if (runReadable) for (const name of [
    'moader-install.json', 'moader-success-retirement.json', 'moader-hen-inspection.json',
    'moader-failure-stop.json', 'moader-listener-attempt.json',
    'moader-device-verification-pending.json', 'ns2jb-modloader-install.json']) {
    const file = path.join(reportRun, name);
    if (realFile(file) && fs.statSync(file).size <= 256000)
      journals.push({name, text: redact(fs.readFileSync(file, 'utf8'))});
  }
  const reportSystem = Object.fromEntries(Object.entries(system()).map(([key, value]) =>
    [key, typeof value === 'string' ? redact(value) : value]));
  const report = {schema: 1, created: now(), Moader: release.version,
    components: checkKit().componentHashes, system: reportSystem,
    providers: {ns2jb: {revision: '28f03b5895e5569d8fbc35cbed84906b8f0a65b2'},
      seedsow: {revision: readJson(path.join(root, 'config/seedsow-provider.json')).revision},
      civswitch: {revision: readJson(path.join(root, 'config/civ7-provider.json')).revision}},
    method: op?.action || null, phase: op?.phase || 'not-started',
    selectedEdition: op?.edition || null,
    nativeOutcome: op?.nativeStarted ? op.status : 'never-started',
    uncertain: state.uncertain && {at: state.uncertain.at,
      method: state.uncertain.method, reason: redact(state.uncertain.reason)},
    launcher: state.launcher && {at: state.launcher.at,
      address: redact(state.launcher.address), online: state.launcher.online,
      error: redact(state.launcher.error || ''), fsVerified: false},
    sdResult: state.sdResult && {...state.sdResult,
      error: redact(state.sdResult.error || '')},
    nativeHistory: state.nativeHistory.map(x => ({method: x.method, at: x.at,
      status: x.status, bootEpoch: x.bootEpoch})),
    firstError: redact(op?.firstError || ''), events: state.events.map(x =>
      ({...x, message: redact(x.message)})), journals};
  const summary = [
    'Moader Setup support report', 'Created: ' + report.created,
    'Version: ' + report.Moader, 'Task: ' + (report.method || 'none'),
    'Stage: ' + report.phase, 'Native operation: ' + report.nativeOutcome,
    'First error: ' + (report.firstError || 'none'),
    'This copy omits saves, game files, pairing codes and binary captures. Redaction is best effort.',
    'Share only after reviewing the advanced report.'
  ].join('\n');
  return {summary, advanced: report};
}
function reply(res, status, body, type = 'application/json; charset=utf-8', headers = {}) {
  const data = Buffer.isBuffer(body) ? body : typeof body === 'string' ? body : JSON.stringify(body);
  res.writeHead(status, {'Content-Type': type, 'Content-Length': Buffer.byteLength(data),
    'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer',
    'X-Content-Type-Options': 'nosniff', 'X-Frame-Options': 'DENY',
    'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; form-action 'none'",
    ...headers});
  res.end(data);
}
async function body(req) {
  let data = '';
  for await (const chunk of req) {
    data += chunk;
    requireTrue(data.length <= 4 * 1024 * 1024, 'Request is too large.');
  }
  return JSON.parse(data || '{}');
}
function openBrowser(url) {
  const program = process.platform === 'darwin' ? 'open' :
    system().wsl ? 'explorer.exe' : 'xdg-open';
  const child = spawn(program, [url], {stdio: 'ignore', detached: true, windowsHide: true});
  child.on('error', error => event('notice', 'Could not open a browser automatically: ' + error.message));
  child.unref();
}
async function listen() {
  const existing = fs.existsSync(lockFile) ? readJson(lockFile) : null;
  if (existing?.port && existing?.token) {
    const check = await new Promise(resolve => {
      const req = http.get({host: '127.0.0.1', port: existing.port, path: '/health',
        headers: {'X-Moader-Token': existing.token}, timeout: 1000},
      res => { res.resume(); resolve(res.statusCode === 200); });
      req.on('error', () => resolve(false));
      req.on('timeout', () => { req.destroy(); resolve(false); });
    });
    if (check) {
      console.log('Moader Setup is ready at http://127.0.0.1:' + existing.port +
        '/?token=' + existing.token);
      if (process.env.MOADER_SETUP_NO_OPEN !== '1')
        openBrowser('http://127.0.0.1:' + existing.port + '/?token=' + existing.token);
      return;
    }
    fs.unlinkSync(lockFile);
  }
  const token = crypto.randomBytes(32).toString('hex');
  const server = http.createServer(async (req, res) => {
    try {
      const host = req.headers.host;
      requireTrue(host && /^127\.0\.0\.1:\d{1,5}$/.test(host), 'Invalid loopback host.');
      const url = new URL(req.url, 'http://' + host);
      const origin = 'http://' + host;
      if (req.method === 'GET' && uiFiles.has(url.pathname)) {
        requireTrue(url.pathname !== '/' || url.searchParams.get('token') === token,
          'This setup page needs its private launch link.');
        const [file, type] = uiFiles.get(url.pathname);
        return reply(res, 200, fs.readFileSync(path.join(root, file)), type);
      }
      requireTrue(req.headers['x-moader-token'] === token, 'Private setup token is missing.');
      if (req.method === 'POST') requireTrue(req.headers.origin === origin &&
        req.headers['content-type']?.startsWith('application/json'), 'Invalid request origin or content type.');
      if (req.method === 'GET' && url.pathname === '/health') return reply(res, 200, {ok: true});
      if (req.method === 'GET' && url.pathname === '/api/state') return reply(res, 200, {
        system: system(), kit: checkKit(), supported: Object.fromEntries(Object.keys(HOST_PROFILES).map(x => [x, supported(x)])),
        hostAddresses: hostAddresses(),
        prefs: state.prefs, operation: state.operation, prepared: state.prepared,
        uncertain: state.uncertain, bootEpoch: state.bootEpoch || 1,
        bootAttempts: state.nativeHistory.filter(x => x.status === 'started' &&
          x.bootEpoch === (state.bootEpoch || 1)).map(x => ({method: x.method,
            at: x.at, status: x.status})),
        selectedSession: state.selectedSession && {method: state.selectedSession.method,
          run: redact(state.selectedSession.run), at: state.selectedSession.at},
        selectedCapture: state.selectedCapture && {sha256: state.selectedCapture.sha256},
        standaloneReady: !!(state.standalonePayloadDir &&
          fs.existsSync(state.standalonePayloadDir)),
        launcher: state.launcher,
        host: state.host && {method: state.host.method, status: state.host.status,
          run: state.host.run && redact(state.host.run)},
        sdPlan: state.sdPlan && {id: state.sdPlan.id, kind: state.sdPlan.kind,
          files: state.sdPlan.files,
          totalBytes: state.sdPlan.totalBytes, at: state.sdPlan.at},
        sdResult: state.sdResult,
        captures: captureCandidates().map(x => ({file: redact(x.file), eligible: x.eligible, reason: x.reason || null})),
        events: state.events.slice(-150).map(x => ({...x, message: redact(x.message)})),
      });
      if (req.method === 'GET' && url.pathname === '/api/report') {
        const report = supportReport(), format = url.searchParams.get('format');
        const data = format === 'text' ? report.summary : JSON.stringify(report.advanced, null, 2) + '\n';
        return reply(res, 200, data, format === 'text' ? 'text/plain; charset=utf-8' :
          'application/json; charset=utf-8', {'Content-Disposition':
          'attachment; filename="Moader-Setup-support.' + (format === 'text' ? 'txt' : 'json') + '"'});
      }
      if (req.method === 'GET' && url.pathname === '/api/stage-file') {
        const index = Number(url.searchParams.get('index'));
        requireTrue(state.sdPlan && Number.isInteger(index) && index >= 0 &&
          index < state.sdPlan.files.length, 'Choose a file from the current SD plan.');
        const row = state.sdPlan.files[index];
        const file = path.join(state.sdPlan.output, row.path);
        requireTrue(realFile(file) && fs.statSync(file).size === row.bytes &&
          sha(fs.readFileSync(file)) === row.sha256, 'The staged source changed.');
        return reply(res, 200, fs.readFileSync(file), 'application/octet-stream');
      }
      if (req.method === 'POST' && url.pathname === '/api/preferences') {
        const value = await body(req);
        if (typeof value.lastMethod === 'string' && Object.hasOwn(HOST_PROFILES, value.lastMethod))
          state.prefs.lastMethod = value.lastMethod;
        if (typeof value.console === 'string' && (value.console === '' || privateIPv4(value.console)))
          state.prefs.console = value.console;
        if (Number.isInteger(value.listenerPort) && value.listenerPort >= 1024 && value.listenerPort <= 65535)
          state.prefs.listenerPort = value.listenerPort;
        save();
        return reply(res, 200, {ok: true});
      }
      if (req.method === 'POST' && url.pathname === '/api/check-launcher') {
        const value = await body(req);
        requireTrue(['demo', 'full'].includes(value.edition), 'Choose the Seedsow edition first.');
        state.launcher = {address: value.address, edition: value.edition,
          at: now(), online: false, error: null};
        save();
        try {
          const result = await checkLauncher(value.address);
          state.launcher = {...state.launcher, at: now(), online: true};
          event('launcher', 'The current Seedsow launcher replied. FS access is not verified.');
          return reply(res, 200, result);
        } catch (error) {
          state.launcher = {...state.launcher, at: now(), error: error.message};
          event('launcher', 'The current Seedsow launcher check failed: ' + error.message);
          throw error;
        }
      }
      if (req.method === 'POST' && url.pathname === '/api/dependency-handoff') {
        const result = await openDependencyManager();
        event('handoff', result.message);
        return reply(res, 200, result);
      }
      if (req.method === 'POST' && url.pathname === '/api/capture/select') {
        const value = await body(req);
        const candidate = captureCandidates()[value.index];
        requireTrue(candidate?.eligible, candidate?.reason || 'Choose a qualified capture.');
        state.selectedCapture = {file: candidate.file, sha256: candidate.sha256, at: now()};
        state.standalonePayloadDir = null;
        state.sdPlan = null;
        state.sdResult = null;
        save();
        return reply(res, 200, {ok: true});
      }
      if (req.method === 'POST' && url.pathname === '/api/capture/import') {
        const value = await body(req);
        const result = importCapture(value.files);
        state.selectedCapture = {file: result.file, sha256: result.sha256, at: now()};
        state.standalonePayloadDir = null;
        state.sdPlan = null;
        state.sdResult = null;
        save();
        return reply(res, 200, {ok: true});
      }
      if (req.method === 'POST' && url.pathname === '/api/stage-result') {
        const value = await body(req);
        requireTrue(state.sdPlan && typeof state.sdPlan.id === 'string' &&
          value.planId === state.sdPlan.id &&
          value.kind === state.sdPlan.kind &&
          Number.isInteger(value.verified) && value.verified >= 0 &&
          value.verified <= state.sdPlan.files.length, 'Invalid SD readback result.');
        state.sdResult = {kind: value.kind, verified: value.verified,
          total: state.sdPlan.files.length, complete: value.verified === state.sdPlan.files.length &&
            value.complete === true, at: now(), source: 'browser directory picker and readback',
          error: value.complete === true ? null : redact(String(value.error || 'The browser stopped before readback completed.').slice(0, 700))};
        event('sd', state.sdResult.complete ? 'SD card files were written and read back.' :
          'SD staging stopped before every file was verified.');
        return reply(res, 200, {ok: true});
      }
      if (req.method === 'POST' && url.pathname === '/api/operation') {
        const value = await body(req);
        requireTrue(typeof value.action === 'string' && value.action.length < 40, 'Choose one setup action.');
        requireTrue(!active, 'A setup operation is already running.');
        runOperation(value.action, value).catch(error => event('error', error.message));
        return reply(res, 202, {started: true});
      }
      if (req.method === 'POST' && url.pathname === '/api/new-boot') {
        const value = await body(req);
        await closeBoot(value.confirmed);
        return reply(res, 200, {ok: true});
      }
      return reply(res, 404, {error: 'No such setup action.'});
    } catch (error) { return reply(res, 400, {error: error.message}); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  writeJson(lockFile, {pid: process.pid, port, token});
  process.on('exit', () => {
    try { if (readJson(lockFile).token === token) fs.unlinkSync(lockFile); } catch {}
  });
  const url = 'http://127.0.0.1:' + port + '/?token=' + token;
  console.log('Moader Setup is ready at ' + url);
  if (process.env.MOADER_SETUP_NO_OPEN !== '1') openBrowser(url);
}
listen().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
