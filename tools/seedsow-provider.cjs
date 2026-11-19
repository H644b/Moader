'use strict';
// Source-only provider identity. Importing this file opens no transport and
// does not execute upstream entrypoints, bridge modules or acquisition code.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const ID = 'seedsow-malev0';
const REVISION = '7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17';
const ARCHIVE = '616660cb65acc703402a212e8691aff84b7f4c228aeedcef80a7763130291976';
const TREE = '4d4375f2729ca4b95a98348419c1e2b7edffa684bf53316f00a38194c5f4ca7b';
const ENGINE = 'ns2jb-main/ns2jb-main/engine';
const DIRECTORY = 'research/sources/seedsow-fs-kaihen';
const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = value => crypto.createHash('sha256').update(value).digest('hex');

function relative(name) {
  check(typeof name === 'string' && name && !path.isAbsolute(name) && !name.includes('\\') &&
    !name.split('/').some(part => !part || part === '.' || part === '..'), 'Unsafe Seedsow provider path');
  return name;
}
function runFiles(files) {
  const result = {};
  for (const [name, digest] of Object.entries(files)) {
    relative(name);
    check(/^[a-f0-9]{64}$/.test(digest), 'Invalid Seedsow file digest');
    if (name.startsWith(ENGINE + '/')) result[name.slice(ENGINE.length + 1)] = digest;
  }
  return result;
}
function resolveProfile(root) {
  const file = path.join(root, 'config/seedsow-provider.json');
  for (let at = file;; at = path.dirname(at)) {
    check(!fs.lstatSync(at).isSymbolicLink(), 'Symlink in Seedsow provider path');
    if (path.dirname(at) === at) break;
  }
  const bytes = fs.readFileSync(file);
  check(bytes.length <= 4 * 1024 * 1024, 'Seedsow descriptor exceeds bound');
  const value = JSON.parse(bytes.toString('utf8'));
  check(value.schemaVersion === 1 && value.id === ID && value.target === 'seedsow' &&
    value.repository === 'Malev0/seedsow-fs-kaihen' && value.revision === REVISION &&
    value.archiveSha256 === ARCHIVE && value.sourceTreeSha256 === TREE &&
    value.sourceDirectory === DIRECTORY && value.engineRelativePath === ENGINE &&
    value.shimRelativePath === 'console/n2h_rpc.lua' &&
    value.sourceTreeAlgorithm === 'sha256-sorted-path-nul-sha256-newline-v1',
  'Seedsow descriptor differs from the reviewed source profile');
  check(value.files && Object.keys(value.files).length === 140 && value.runFiles &&
    Object.keys(value.runFiles).length === 130, 'Incomplete Seedsow source manifest');
  const entries = Object.entries(value.files).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0);
  check(sha(entries.map(([name, digest]) => relative(name) + '\0' + digest + '\n').join('')) === TREE,
    'Seedsow source manifest tree changed');
  const derived = runFiles(value.files), names = Object.keys(derived);
  check(names.length === 130 && names.every(name => derived[name] === value.runFiles[name]),
    'Seedsow runtime manifest is not derived from the reviewed sources');
  return {...value, sourceDirectory: path.join(root, DIRECTORY)};
}

let live = null;
function ownedFile(run, relative) {
  relative = relative.split(path.sep).join('/');
  check(relative && !relative.split('/').some(part => !part || part === '.' || part === '..'),
    'Seedsow owned path escaped the run');
  const file = path.join(run, relative);
  for (let at = file;; at = path.dirname(at)) {
    check(!fs.lstatSync(at).isSymbolicLink(), 'Symlink in Seedsow run proof');
    if (at === run) break;
    check(at.startsWith(run + path.sep), 'Seedsow run proof escaped its owner');
  }
  check(fs.lstatSync(file).isFile(), 'Missing Seedsow run proof: ' + relative);
  return file;
}
function readOwned(run, relative) {
  const bytes = fs.readFileSync(ownedFile(run, relative));
  check(bytes.length <= 8 * 1024 * 1024, 'Oversized Seedsow run proof');
  return JSON.parse(bytes.toString('utf8'));
}
function linked(run, file) {
  check(typeof file === 'string' && path.isAbsolute(file) && file.startsWith(run + path.sep),
    'Seedsow proof path is outside its run');
  return readOwned(run, path.relative(run, file));
}
function validateHeld(run, root, {retired = false} = {}) {
  check(live && live.run === run && live.root === root && live.guard.error === null &&
    live.owner?.probePromise && live.owner.probeSettled === false &&
    live.endpoint.socketPath && live.endpoint.socketIdentity,
  'Seedsow live installation is not admitted yet: a Moader-owned, still-running FS-proven producer is required');
  const profile = resolveProfile(root);
  const source = readOwned(run, 'moader-source.json');
  check(source.provider === ID && source.revision === REVISION && source.sourceTreeSha256 === TREE &&
    Object.keys(source.files || {}).length === 130,
  'Seedsow materialized source identity differs');
  const sourceLoader = require('./seedsow-source-loader.cjs');
  const transformed = sourceLoader.TRANSFORMED;
  for (const [relative, expected] of Object.entries(profile.runFiles)) {
    const pin = transformed[relative] || expected;
    check(source.files[relative] === pin &&
      sha(fs.readFileSync(ownedFile(run, relative))) === pin,
    'Seedsow materialized run code changed: ' + relative);
  }
  const fixture = sourceLoader.TEST_FUNCTION;
  check(Object.keys(source.derived || {}).length === 1 &&
    JSON.stringify(source.derived[fixture.target]) === JSON.stringify(fixture) &&
    sha(fs.readFileSync(ownedFile(run, fixture.target))) === fixture.sha256,
  'Seedsow derived native test function changed');
  const connectionFile = ownedFile(run, 'connection.json'), connectionBytes = fs.readFileSync(connectionFile);
  const connection = JSON.parse(connectionBytes);
  check(connection.schema === 1 && connection.provider === ID &&
    connection.addresses?.session === 'seedsow' && connection.addresses.platform === 2 &&
    connection.shimSha256 === profile.files['console/n2h_rpc.lua'] &&
    Number.isSafeInteger(connection.challenge) && connection.challenge > 0 &&
    sha(connectionBytes) === live.connectionSha256,
  'Seedsow immutable connection or fresh challenge differs');
  const mountProof = readOwned(run, 'moader-mounts.json');
  check(mountProof.schema === 1 && mountProof.session === 'seedsow' &&
    mountProof.scope === 'save-only' &&
    mountProof.sdk === connection.addresses.sdk &&
    mountProof.file === 'switch2/app/mounts.json' &&
    mountProof.inspectorSha256 === transformed['switch2/app/inspect-mounts.cjs'] &&
    mountProof.sha256 === sha(fs.readFileSync(ownedFile(run, mountProof.file))) &&
    Array.isArray(mountProof.mounts) && mountProof.mounts.length === 1 &&
    mountProof.mounts[0] === 'save',
  'Seedsow generated mount inspection differs from the current owner');
  const marker = readOwned(run, 'moader-seedsow-session.json');
  const current = readOwned(run, 'switch2/fs-execution-current.json');
  const acquisition = readOwned(run, 'switch2/current-acquisition.json');
  const execution = readOwned(run, 'switch2/ro/current-execution.json');
  const proof = linked(run, current.file), report = linked(run, acquisition.report);
  check(marker.schema === 1 && marker.target === 'seedsow' &&
    marker.provider === ID && marker.sourceTreeSha256 === TREE &&
    marker.connectionSha256 === live.connectionSha256 &&
    marker.ownerNonce === live.ownerNonce && marker.producerPid === process.pid &&
    marker.report === acquisition.report && marker.proof === current.file &&
    marker.endpointIdentity === live.endpoint.socketIdentity &&
    marker.socketPath === live.endpoint.socketPath,
  'Seedsow held ownership marker differs from the live producer');
  check(acquisition.session === 'seedsow' && acquisition.socketPath === marker.socketPath &&
    current.verified === true && current.restored === true && current.cleanup?.deferred === true &&
    current.retainedEndpoint?.ready === true && current.retainedEndpoint.socketPath === marker.socketPath &&
    proof.session === 'seedsow' && proof.report === acquisition.report &&
    proof.verified === true && proof.identity?.programId === '0400000000000000' &&
    proof.notifier && proof.mapping && Array.isArray(proof.trials) && proof.trials.length === 2 &&
    proof.trials.every(trial => trial.verified === true && trial.restored === true && !trial.pending),
  'Seedsow FS instruction execution/restoration proof is incomplete');
  check(report.session === 'seedsow' && !report.error && !report.cleanupError &&
    !report.cleanedUp && !report.placementUncertain &&
    report.memoryControl?.ready === true && !report.memoryControl.pending &&
    report.memoryControl.socketPath === marker.socketPath &&
    report.retainedMemory?.ready === true && report.retainedMemory.zeroCountVerified === true &&
    !report.retainedMemory.pending,
  'Seedsow FS memory endpoint is not a ready retained owner');
  check(execution.session === 'seedsow' && execution.report === acquisition.report &&
    execution.programId === '0400000000000037' &&
    (execution.verified === true || retired && execution.retiredForKaihen === true),
  'Seedsow diagnostic RO identity is missing or stale');
  return {profile, run, root, runtimeRoot: run, target: 'seedsow', current, proof,
    execution, report, connectionSha256: live.connectionSha256,
    loader: live.loader, guard: live.guard, endpoint: live.endpoint, retired};
}
function admitHeld({run, root, loader, guard, endpoint, owner, execution, connectionSha256}) {
  check(!live && typeof run === 'string' && path.isAbsolute(run) &&
    /^switch-[A-Za-z0-9]+$/.test(path.basename(run)) &&
    fs.realpathSync(run) === run && owner?.run === run && owner.loader === loader &&
    owner.probePromise && owner.probeSettled === false && guard?.error === null &&
    endpoint?.guard === guard && endpoint.socketPath && endpoint.socketIdentity,
  'Seedsow FS hold does not have one live owner');
  check(execution?.verified === true && execution.restored === true &&
    typeof execution.file === 'string', 'Seedsow FS challenge summary is incomplete');
  const proof = linked(run, execution.file);
  check(proof.verified === true && proof.identity?.programId === '0400000000000000' &&
    proof.trials?.length === 2 && proof.trials.every(row => row.verified && row.restored && !row.pending),
  'Seedsow FS trials did not verify and restore');
  const acquisition = readOwned(run, 'switch2/current-acquisition.json');
  check(proof.report === acquisition.report && endpoint.socketPath === acquisition.socketPath,
    'Seedsow FS proof and endpoint owner differ');
  const current = {...execution, file: execution.file, cleanup: {deferred: true},
    retainedEndpoint: {ready: true, socketPath: endpoint.socketPath}};
  fs.writeFileSync(path.join(run, 'switch2/fs-execution-current.json'),
    JSON.stringify(current, null, 2) + '\n', {flag: 'wx', mode: 0o600});
  const ownerNonce = crypto.randomBytes(32).toString('hex');
  const marker = {schema: 1, target: 'seedsow', provider: ID, sourceTreeSha256: TREE,
    connectionSha256, ownerNonce, producerPid: process.pid,
    report: acquisition.report, proof: execution.file,
    endpointIdentity: endpoint.socketIdentity, socketPath: endpoint.socketPath};
  fs.writeFileSync(path.join(run, 'moader-seedsow-session.json'),
    JSON.stringify(marker, null, 2) + '\n', {flag: 'wx', mode: 0o600});
  live = {run, root, loader, guard, endpoint, owner, ownerNonce, connectionSha256};
  try { return validateHeld(run, root); }
  catch (error) { live = null; throw error; }
}
function qualifyRun(run, root, options = {}) {
  check(typeof run === 'string' && path.isAbsolute(run),
    'Seedsow live installation is not admitted yet: no fresh run was supplied');
  run = path.resolve(run);
  return validateHeld(run, root, options);
}
module.exports = {ID, REVISION, ARCHIVE, TREE, ENGINE, DIRECTORY, resolveProfile,
  runFiles, qualifyRun, admitHeld};
