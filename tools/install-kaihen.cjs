#!/usr/bin/env node
// Upgrade an already verified, held ns2jb diagnostic session to kaiHEN.
// Uses upstream's checked Loader gate, process activation and bootstrap.
const fs = require('node:fs/promises');
const path = require('node:path');
const Module = require('node:module');
const rootDir = path.resolve(__dirname, '..');
const inputs = require('./install-inputs.cjs');
function guardKaihenTransports(run, load, host) {
  if (host?.target === 'seedsow') {
    if(host.guard?.error||host.loader?.run!==path.resolve(run))
      throw Error('Seedsow HEN startup lost its live source-owned transport');
    return host.guard;
  }
  if (host) return require('./host-transport.cjs').install(host);
  const {guardTransports} = require('./ready-kaihen.cjs');
  const state = guardTransports({rpc:load('shared/rpc.cjs'),endpoint:load('switch2/tools/endpoint.cjs')});
  // The independent RO observer loads the selected run's SDK/IPC modules.
  // Both module trees must stop at the first uncertain native operation.
  guardTransports({rpc:require(path.join(run,'shared/rpc.cjs')),
    endpoint:require(path.join(run,'switch2/tools/endpoint.cjs'))},state);
  return state;
}
async function qualifyRelease(prepared) {
  const archive = await fs.readFile(path.join(rootDir, 'research/artifacts/kaiHEN-20261114c.zip'));
  if (inputs.digest(archive) !== '63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967')
    throw Error('kaiHEN release checksum differs');
  const build=await inputs.qualifyKaihenBuild(rootDir);
  if(prepared){
    inputs.assertKaihenBinding(prepared,build);
    for(const [name,pin] of Object.entries(build.components)){
      const item=prepared.kaihenFiles.find(row=>row.source===path.join(prepared.kaihen,name));
      if(item?.sha256!==pin.sha256||(pin.bytes!==undefined&&item?.bytes!==pin.bytes))
        throw Error('Prepared '+name+' differs from the qualified kaiHEN release/build proof');
    }
  }
  return build;
}
async function prepareKaihen({ship,extraFile,output}) {
  // Reject all extra manifests before loading a module with a live endpoint.
  const extra = await inputs.readExtra(extraFile);
  const build=await inputs.qualifyKaihenBuild(rootDir);
  const prepared = await inputs.snapshot({output,kaihen:ship,programs:extra,kaihenBuild:build});
  await inputs.validatePrepared(prepared);
  await qualifyRelease(prepared);
  return prepared;
}
function verifyRoPackage(packageSession,image,policy) {
  if (packageSession.result || !Array.isArray(packageSession.entries) || packageSession.entries.length !== 2)
    throw Error('RO package did not return both expected entries');
  for (const [name,bytes] of [['main',image],['main.npdm',policy]]) {
    const matches=packageSession.entries.filter(item=>item.name===name);
    if (matches.length !== 1 || matches[0].size !== bytes.length || matches[0].sha256 !== inputs.digest(bytes))
      throw Error('RO package '+name+' readback differs from its qualified input');
  }
}
async function qualifyStagedRoReuse(run, report, candidate, image, policy) {
  // Seedsow has already staged this package as the diagnostic RO donor. Its
  // open CodeFS locks the SD path, so deleting it here returns fs::TargetLocked.
  // Reuse only the exact package verified by this held acquisition.
  const donor = report?.nativeRo?.donor;
  const previous = path.join(run, 'switch2/ro/seedsow-kaihen-ro-exefs.nsp');
  const staged = donor?.package?.staged;
  if (process.env.NS2_KAIHEN_SKIP_STAGE === '1' ||
      donor?.ready !== true || donor.mount !== 'package' || donor.kind !== 'ro' ||
      donor.package.file !== previous ||
      staged?.path !== '@Sdcard:/kaihen/bin/ro/exefs.nsp' ||
      !Number.isSafeInteger(staged.size) || staged.size <= 0 ||
      !/^[0-9a-f]{64}$/.test(staged.sha256 || '') ||
      !Array.isArray(donor.files) || donor.files.some(row => row.verified !== true))
    throw Error('This held Seedsow run has no source-qualified staged RO donor');
  verifyRoPackage({result: 0, entries: donor.files}, image, policy);
  const [priorBytes, candidateBytes] = await Promise.all([fs.readFile(previous), fs.readFile(candidate)]);
  const priorSha = inputs.digest(priorBytes), candidateSha = inputs.digest(candidateBytes);
  if (priorBytes.length !== staged.size || donor.package.size !== staged.size ||
      priorSha !== staged.sha256 || candidateSha !== priorSha ||
      !priorBytes.equals(candidateBytes) ||
      donor.image?.sha256 !== inputs.digest(image) ||
      donor.policy?.sha256 !== inputs.digest(policy))
    throw Error('Held Seedsow RO donor differs from the matched kaiHEN package');
  return {path: staged.path, size: staged.size, sha256: priorSha,
    source: previous, reusedFromHeldDonor: true};
}
async function main(args = process.argv.slice(2)) {
const run = args[0];
if (!run || args.length > 3) throw Error('Usage: node tools/install-kaihen.cjs RUN_DIRECTORY [KAIHEN_DIRECTORY] [EXTRA_MANIFEST]');
const prepared = await prepareKaihen({
  ship: args[1] ? path.resolve(args[1]) : path.join(rootDir, 'vendor/kaihen'),
  extraFile: args[2] ? path.resolve(args[2]) : undefined,
  output: path.join(rootDir, 'build/install'),
});
const programs = [...prepared.bundledPrograms, ...prepared.programs].map(item => ({source:item.source,target:item.target}));
const stagedInputs = new Map();
for (const [target,name] of [['/hbmenu.nro','hbmenu.nro'],['/kaihen/bin/game/main','game/main'],['/kaihen/bin/hen/main','hen/main']]) {
  const item = prepared.kaihenFiles.find(row => row.source === path.join(prepared.kaihen,name));
  stagedInputs.set(target, await inputs.readInput(item,true));
}
for (const item of [...prepared.bundledPrograms,...prepared.programs]) stagedInputs.set(item.target, await inputs.readInput(item,true));
process.env.NS2_RUN_DIR = path.resolve(run);
const hosts = require('./host-session.cjs');
const profileId = hosts.inferredId(run);
const host = profileId===hosts.DEFAULT ? null : hosts.qualifyRun(run,profileId);
if(host) hosts.bind(host);
const upstream = host?.runtimeRoot || path.join(rootDir, 'research/sources/ns2jb/engine');
const ship = prepared.kaihen;
const runName = path.basename(process.env.NS2_RUN_DIR);
const output = path.join(rootDir, 'evidence/kaihen/install-' + runName + '.json');
const load = rel => host ? hosts.load(host,rel) : require(path.join(upstream, rel));
const transportGuard = guardKaihenTransports(process.env.NS2_RUN_DIR,load,host);
const {Session, live, check} = load('switch2/app/session.cjs');
const domain = load('switch2/app/domain.cjs');
const filesystem = load('switch2/app/filesystem.cjs');
const memModule = load('switch2/tools/fs-memory.cjs');
const packages = load('switch2/ro/package.cjs');
const {verifySdFile} = require('./sd-install.cjs');
const {guardSession} = require('../runtime/modloader/lib/game-files/uncertain.cjs');
const record = {session: live.session, run: process.env.NS2_RUN_DIR,
  runtime: '20261114c', packageDirectory: ship, started: new Date().toISOString(), steps: [], complete: false};
const persist = () => fs.writeFile(output, JSON.stringify(record, null, 2) + '\n', {mode: 0o600});
const read = file => fs.readFile(file, 'utf8').then(JSON.parse);
async function step(name, action) {
  console.log(name);
  const row = {name, started: new Date().toISOString(), status: 'pending'};
  record.steps.push(row); await persist();
  try { row.result = await action(); row.status = 'complete'; return row.result; }
  catch (e) { row.status = 'failed'; row.error = e.stack; throw e; }
  finally { row.finished = new Date().toISOString(); await persist(); }
}

let journalCreated = false;
async function execute() {
  await fs.mkdir(path.dirname(output), {recursive: true});
  // A completed or uncertain install must be inspected, never replayed.
  await fs.writeFile(output, JSON.stringify(record, null, 2), {flag: 'wx', mode: 0o600});
  journalCreated = true;
  record.preparedManifest = prepared.manifestFile;
  const memory = await memModule.connect();
  const nativeRequest = memory.request;
  const roDir = path.join(process.env.NS2_RUN_DIR, 'switch2/ro');
  const execution = await read(path.join(roDir, 'current-execution.json'));
  record.previousExecution = execution;
  check(execution.verified && execution.session === memory.info.session &&
    execution.report === memory.reportFile, 'This session has no verified diagnostic RO');
  const runtime = load('switch2/ro/runtime.cjs');
  await step('Verify the current Loader session', () => runtime.handoff());
  // Qualify package reuse from the held report and both host byte streams
  // before allocating another application session or sending an SD operation.
  const roPackage = path.join(rootDir, 'evidence/kaihen/ro-exefs-' + runName + '.nsp');
  const policyPath = path.join(roDir, live.session + '-code-0400000000000037/main.npdm');
  const expectedRo = await inputs.readInput(prepared.kaihenFiles.find(item=>item.source===path.join(ship,'ro/main')),true);
  const expectedPolicy = await fs.readFile(policyPath);
  record.roInputs = {imageSha256:inputs.digest(expectedRo.data),policySha256:inputs.digest(expectedPolicy)};
  await step('Build RO package with this console’s signed policy', () => (host?require('./host-kaihen.cjs').buildPackage:load('switch2/ro/package-build.cjs').build)(
    expectedRo.source, policyPath, roPackage));
  if (host?.target === 'seedsow') {
    record.roPackageReuse = await step('Verify and reuse held Seedsow RO package', () =>
      qualifyStagedRoReuse(process.env.NS2_RUN_DIR, memory.report, roPackage,
        expectedRo.data, expectedPolicy));
  }

  const s = guardSession(new Session('moader-kaihen-install', 0x30000));
  await s.initialize();
  // Clone the initialized root; do not send SetCurrentProcess on the shared root.
  const rootHandle = memory.report.ownedRoot.clones[0];
  const ownedRoot = domain.oneHandle(await domain.send(s, 'Clone initialized FS root', rootHandle,
    domain.wire.control(2)));
  const localState = {};
  const context = {s, live, save: async () => { record.packageState = localState; await persist(); }};
  let packageSession;
  memory.request = async req => {
    if (['root-native-file-stage', 'root-native-package-stage', 'root-native-package-open'].includes(req.command)) {
      const qualified = req.command === 'root-native-file-stage' ? stagedInputs.get(req.target) : undefined;
      if (req.command === 'root-native-file-stage') {
        check(qualified && qualified.source === req.source, 'File stage is outside the prepared snapshot');
        await inputs.readInput(qualified,true);
      }
      const result = await packages.dispatch(context, {...req, root: ownedRoot}, localState, domain);
      if (qualified) result.readback = await verifySdFile({s,root:ownedRoot,domain,filesystem,target:req.target,expected:qualified.data});
      return result;
    }
    const value = await nativeRequest(req);
    if (req.command === 'root-native-info' && packageSession) return {...value, donor: localState.donor};
    return value;
  };
  // The bootstrap uses the same checked endpoint; the older host need not be modified.
  memModule.connect = async () => memory;
  if (host?.target !== 'seedsow') {
    await step('Stage kaiHEN RO package on SD', () => memory.request({command: 'root-native-package-stage',
      root: rootHandle, kind: 'ro', source: roPackage}));
  }
  packageSession = await step('Open and verify RO package', () => memory.request({command: 'root-native-package-open',
    root: rootHandle, kind: 'ro', selectDonor: true}));
  verifyRoPackage(packageSession,expectedRo.data,expectedPolicy);

  const typesPath = path.join(roDir, live.session + '-filesystem-types.json');
  const oldTypes = await read(typesPath);
  record.previousTypes = oldTypes;
  await fs.writeFile(path.join(rootDir, 'evidence/kaihen/previous-filesystem-types-' + runName + '.json'),
    JSON.stringify(oldTypes, null, 2), {flag: 'wx', mode: 0o600});
  const layout = await read(path.join(roDir, live.session + '-layout.json'));
  check(packageSession.root > 0 && packageSession.root <= layout.count, 'Package domain ID out of bounds');
  const entry = layout.table + (packageSession.root - 1) * 0x30;
  const raw = await memory.read(entry, 0x30);
  const ptr = (b, at = 0) => Number(b.readBigUInt64LE(at));
  const object = ptr(raw, 0x20);
  const header = await memory.read(object, 0x40);
  const codeType = oldTypes.rows.find(e => e.name === 'code');
  const donor = {name: 'donor', handle: packageSession.handle, root: packageSession.root,
    entry, domain: ptr(raw, 0x18), object, metadata: ptr(raw, 0x28), vptr: ptr(header),
    raw: raw.toString('hex'), header: header.toString('hex')};
  check(donor.domain > 2 ** 32 && donor.metadata === codeType.metadata && donor.vptr === codeType.vptr,
    'Package and CodeFS adapter types differ');
  await fs.writeFile(typesPath, JSON.stringify({...oldTypes, rows: [donor, codeType]}, null, 2));

  // Load the current upstream gate algorithm with run paths instead of source paths.
  // Keep the source checkout unchanged and preserve its relative dependencies.
  let launched;
  const loaderRoot = await read(path.join(roDir,live.session+'-loader-root-copy.json'));
  const loaderBindings = await read(path.join(roDir,live.session+'-bound-sessions.json'));
  check(loaderRoot.report===memory.reportFile && loaderBindings.report===memory.reportFile,
    'Loader observer bindings belong to another session');
  const loaderRows = loaderBindings.rows.filter(row=>row.domain===loaderRoot.targetDomain);
  check(loaderRows.length===1,'The bound Loader session is not unique');
  record.loaderObserver = {resourcesRetained:false};
  const observer = await step('Open independent Loader restoration observer', () =>
    require('./loader-observer.cjs').openObserver({run:process.env.NS2_RUN_DIR,memory,execution,
      state:transportGuard,record:record.loaderObserver,save:persist}));
  try {
    await step('Qualify independent Loader view before parking',()=>observer.qualify(loaderRows[0]));
    // Only the temporary exported Gate class changes. Qualified provider source
    // bytes remain untouched, including the Civ7 materialized runtime.
    launched = await require('./loader-restoration.cjs').withGateRestoration(
      load('switch2/ro/gate.cjs'),observer,async () => {
        if(host) return step('Launch the checked kaiHEN RO image', () => require('./host-kaihen.cjs').launch(host,memory));
        const launchFilename = path.join(upstream, 'switch2/ro/launch.cjs');
        let launchCode = await fs.readFile(launchFilename, 'utf8');
        launchCode = "const {runPath} = require('../../shared/paths.cjs');\n" + launchCode
          .replaceAll('path.join(__dirname, memory.info.session', "runPath('switch2', 'ro', memory.info.session");
        const launchModule = new Module(launchFilename, module);
        launchModule.filename = launchFilename;
        launchModule.paths = Module._nodeModulePaths(path.dirname(launchFilename));
        launchModule._compile(launchCode, launchFilename);
        return step('Launch the checked kaiHEN RO image', () => launchModule.exports.main({memory}));
      });
    const receipt = launched.restoration;
    if (!launched.restored || !receipt?.receiptSeen || !receipt.snapshotRetired ||
        receipt.unresolved || receipt.clear?.result!==0) {
      const error = Error('RO launch has no checked Loader restoration receipt');
      observer.block(error);
      throw error;
    }
  } finally {
    // This closes only after a fully acknowledged restoration and joined PM
    // worker. An uncertain observer leaves all owned native state retained.
    if (!transportGuard.error && !observer.uncertain && launched?.restored)
      await step('Close Loader restoration observer', () => observer.close());
    else { record.loaderObserver.resourcesRetained = true; await persist(); }
  }
  check(launched.imageAccepted && launched.restored, 'RO image was not accepted and restored');
  record.ro = await step('Activate kaiHEN RO and retire the identified diagnostic RO', () =>
    host ? require('./host-kaihen.cjs').activate(host,launched.file,execution.pid,memory) : runtime.activate(launched.file, execution.pid));
  await fs.writeFile(path.join(roDir, 'current-execution.json'), JSON.stringify({
    ...execution, verified: false, retiredForKaihen: true, kaihenPid: record.ro.pid,
    installation: output,
  }, null, 2) + '\n', {mode: 0o600});
  if(host)host.retired=true;
  await step('Verify kaiHEN ldr:ro initialization', () => require('./ready-kaihen.cjs').ready({
    Session, argumentsWithPid:load('switch2/ro/nro.cjs').argumentsWithPid,
  }));
  await step('Verify restored Loader session', () => runtime.handoff());
  process.env.HBMENU_NRO = path.join(ship, 'hbmenu.nro');
  record.hen = await step('Stage hbmenu and start the native HEN bootstrap', () => (host ? (...args)=>require('./host-kaihen.cjs').startHen(host,...args) : load('switch2/ro/start-hen.cjs').main)(
    path.join(ship, 'hen/main'), path.join(ship, 'bootstrap.bin'), path.join(ship, 'game/main'), programs));
  record.startup = await read(record.hen.file);
  check(record.startup.operation === 10 && record.startup.result === 0, 'HEN startup did not complete');
  await s.cleanup();
  record.complete = true;
  record.finished = new Date().toISOString();
  await persist();
  console.log('HEN startup check passed.');
}
try { await execute(); }
catch (error) {
  if (journalCreated) {
    record.error = error.stack;
    record.nativeOperationUncertain = !!transportGuard.error;
    record.inspectBeforeRetry = true;
    try { await persist(); } catch {}
  }
  console.error('HEN setup stopped. Check the report before trying again: ' + output);
  throw error;
}
}
module.exports = {main,prepareKaihen,qualifyRelease,verifyRoPackage,
  qualifyStagedRoReuse,guardKaihenTransports};
if (require.main === module) main().catch(error => {console.error(error.stack);process.exitCode = 1;});
