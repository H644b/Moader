// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const token = new URL(location.href).searchParams.get('token');
const main = document.getElementById('main');
const toast = document.getElementById('toast');
const announcer = document.getElementById('announcer');
let snapshot = null, task = 'home', method = 'seedsow', renderKey = '',
  sdHandle = null, sdExisting = [], lastPlanId = null, initialRefreshDone = false,
  lastAnnouncedOperation = null;
const draft = {edition: 'demo', console: '', hostAddress: '', statusCommand: '',
  gameReady: false, profileConfirmed: false, backupConfirmed: false,
  civCartridge: false, civFirmware: false, civLoader: false, bootRebooted: false};
const taskNames = {
  home: 'Home', boot: 'Set up for this boot', standalone: 'Prepare computer-free boots',
  copy: 'Copy app and plugin', reload: 'Update running service',
  connection: 'Check connection', browser: 'Open Moader in browser',
  report: 'Export support report'
};
const methodNames = {
  seedsow: 'Seedsow through this computer', mac2jb: 'SmileBASIC through mac2jb',
  ns2jb: 'SmileBASIC through direct ns2jb', civswitch: 'Experimental CivSwitch'
};
const safe = value => String(value ?? '').replace(/[&<>"']/g, c =>
  ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const api = async (path, value) => {
  const response = await fetch(path, {method: value === undefined ? 'GET' : 'POST',
    headers: {'X-Moader-Token': token, ...(value === undefined ? {} :
      {'Content-Type': 'application/json'})},
    body: value === undefined ? undefined : JSON.stringify(value), cache: 'no-store'});
  if (!response.ok) {
    let message = 'The setup controller could not complete this action.';
    try { message = (await response.json()).error || message; } catch {}
    throw Error(message);
  }
  return response;
};
const post = async (path, value) => (await api(path, value)).json();
const show = message => {
  toast.textContent = message; toast.classList.add('show');
  announcer.textContent = message;
  clearTimeout(show.timer); show.timer = setTimeout(() => toast.classList.remove('show'), 7000);
};
const heading = (title, description) => '<h1>' + title + '</h1><p class="intro">' + description + '</p>';
const note = (title, body, type = '') => '<div class="note ' + type + '"><strong>' +
  title + '</strong><p>' + body + '</p></div>';
const button = (label, action, kind = 'primary', disabled = false) =>
  '<button type="button" class="' + kind + '" data-action="' + action + '"' +
  (disabled ? ' disabled' : '') + '>' + label + '</button>';
const field = (label, key, placeholder, hint = '', type = 'text') =>
  '<label class="field"><span>' + label + '</span><input type="' + type + '" data-draft="' +
  key + '" value="' + safe(draft[key]) + '" placeholder="' + safe(placeholder) +
  '" autocomplete="off">' + (hint ? '<small>' + hint + '</small>' : '') + '</label>';
const check = (key, label) => '<label class="check"><input type="checkbox" data-draft="' +
  key + '"' + (draft[key] ? ' checked' : '') + '><span>' + label + '</span></label>';
const osLabel = () => {
  const s = snapshot.system;
  return (s.wsl ? 'Windows with WSL2 · Linux tools' : s.os === 'darwin' ? 'macOS' :
    s.os === 'linux' ? 'Linux' : 'Unsupported host') + ' · ' + s.arch;
};
const osDirections = selected => {
  const s = snapshot.system;
  if (selected === 'seedsow') {
    if (s.wsl) return 'This runs inside WSL2 with Linux Node and Linux paths. The computer connects outbound to the Seedsow launcher on TCP 8080 and 39380. Keep this kit in the WSL filesystem.';
    if (s.os === 'darwin') return 'This Apple Silicon Mac prepares the matched files here. The Switch and Mac must be on the same local network.';
    return 'This Linux x86_64 computer prepares the matched files here. The Switch and computer must be on the same local network.';
  }
  if (selected === 'mac2jb') return 'mac2jb is a macOS route. Keep its current Node 24 host and SmileBASIC open while Moader installs.';
  if (selected === 'ns2jb' && s.wsl) return 'Use a Linux address that this WSL distro can bind and the Switch can reach. Mirrored networking and a port-specific firewall rule may be needed before acquisition; NAT forwarding alone does not make an address bindable in WSL.';
  if (selected === 'ns2jb') return 'Choose this computer’s LAN IPv4. The provider binds TCP 39380 and shows the SmileBASIC steps after you start it.';
  if (selected === 'civswitch' && s.wsl) return 'The loader is on the Switch at TCP 9026. The console must also reach the host listener at TCP 39380. WSL return-address behavior is not hardware-tested.';
  return 'The loader waits on the Switch at TCP 9026. A verified held FS-only session is needed before Moader installation.';
};
const latestSdResult = () => snapshot.operation?.status !== 'running' && snapshot.sdResult &&
  (!snapshot.operation?.finished ||
    new Date(snapshot.sdResult.at) >= new Date(snapshot.operation.finished))
  ? snapshot.sdResult : null;
const belongsToTask = op => task === 'home' ||
  task === 'boot' && ['prepare', 'start-host', 'seedsow-install',
    'prepare-held', 'install-held'].includes(op.action) ||
  task === 'standalone' && ['generate-standalone', 'upload-standalone',
    'verify-standalone-save'].includes(op.action) ||
  task === 'copy' && op.action === 'export-sd' && snapshot.sdPlan?.kind === 'app';
const operationTask = op => op.action === 'export-sd' ?
  snapshot.sdPlan?.kind === 'standalone' ? 'standalone' : 'copy' :
  ['generate-standalone', 'upload-standalone', 'verify-standalone-save'].includes(op.action)
    ? 'standalone' : 'boot';
const operationView = () => {
  const sd = latestSdResult();
  if (sd && task === 'home')
    return note(sd.complete ? 'SD readback complete' : 'SD staging incomplete',
    safe(sd.verified) + ' of ' + safe(sd.total) + ' files were read back. ' +
    (sd.complete ? 'Eject the card safely. This did not install a backend on the Switch.' :
      '<strong>First error:</strong> ' + safe(sd.error || 'Readback stopped.') +
      '<br>The card may contain a partial update. Inspect it and keep the backup before trying again.'),
    sd.complete ? 'success' : 'error');
  if (sd && (task === 'copy' && sd.kind === 'app' ||
      task === 'standalone' && sd.kind === 'standalone')) return '';
  const op = snapshot.operation;
  if (!op || !belongsToTask(op)) return '';
  const cls = op.status === 'complete' ? 'success' : op.status === 'uncertain' ||
    op.status === 'failed' ? 'error' : 'warning';
  let body = safe(op.message);
  if (op.firstError) body += '<br><strong>First error:</strong> ' + safe(op.firstError);
  if (op.status === 'running' || op.status === 'waiting')
    body += '<br>Keep this window open if convenient. The controller continues if the window closes; reopen Moader Setup to reconnect.';
  if (op.status === 'uncertain')
    body += '<br>Do not repeat a native action on this boot. Export the report, keep the original journal, and follow reboot recovery.';
  return note('Current result · ' + safe(op.phase.replaceAll('-', ' ')), body, cls);
};
const bootBanner = () => {
  const attempts = snapshot.bootAttempts || [];
  if (!attempts.length) return '';
  return note(snapshot.uncertain ? 'A console result needs recovery' : 'This boot has a recorded attempt',
    (snapshot.uncertain ? 'Keep the original host reports. A timeout or lost reply may already have changed the Switch. ' :
      'The prior native attempt remains recorded. ') +
    'After a full Switch reboot, confirm it here to start a new boot record. Earlier journals stay intact.',
    'warning') +
    check('bootRebooted', 'I fully powered off or rebooted the Switch and opened a fresh boot.') +
    '<div class="button-row">' + button('Start a new boot record', 'new-boot', 'secondary',
      !draft.bootRebooted) + '</div>';
};
function home() {
  const items = [
    ['boot','Set up Moader for this boot','Choose Seedsow, SmileBASIC or experimental CivSwitch.'],
    ['standalone','Prepare later computer-free boots','Make the on-console Seedsow A/B files from an eligible capture.'],
    ['copy','Copy app and plugin files','Put the matched console files on a mounted SD card.'],
    ['reload','Update a running service','Follow the checked reload flow in the Moader app.'],
    ['connection','Check connection and setup state','See what the computer can verify and what to check on the Switch.'],
    ['browser','Open Moader in browser','Use the listener address shown by the Switch.'],
    ['report','Collect a support report','Review and save a redacted copy of setup details.']
  ];
  return heading('Set up Moader', 'Choose a task for this computer or your Switch.') +
    bootBanner() + operationView() +
    '<div class="task-list">' + items.map(item =>
      '<button class="task-row" type="button" data-task="' + item[0] + '"><span><strong>' +
      item[1] + '</strong><small>' + item[2] + '</small></span><span class="arrow" aria-hidden="true"></span></button>').join('') +
    '</div>' + note('What survives a reboot', 'Your files and preferences stay on SD. The filesystem runtime and resident service must be started again for a fresh boot.') +
    help('The backend kit already includes the matched app and resident binaries. A successful full backend installation copies them as part of that installer. The app-only ZIP is for an existing compatible backend.');
}
function help(text) {
  return '<details id="stepHelp" class="step"><summary>More help for this step</summary><p>' + text + '</p></details>';
}
function boot() {
  const selected = method, ready = snapshot.prepared[selected]?.version === snapshot.kit.version;
  const allowed = snapshot.supported[selected];
  const picker = '<label class="method-picker"><span>Setup method</span><select id="methodSelect">' +
    Object.entries(methodNames).map(([key, label]) => '<option value="' + key + '"' +
      (key === selected ? ' selected' : '') + '>' + safe(label) + '</option>').join('') +
    '</select></label>';
  const list = Object.entries(methodNames).map(([key, name]) =>
    '<button type="button" class="method ' + (key === selected ? 'selected' : '') +
    '" data-method="' + key + '" aria-pressed="' + (key === selected) + '"><strong>' + name +
    '</strong><span>' + (key === 'seedsow' ? 'Seedsow Demo or full game · one computer-driven install' :
    key === 'mac2jb' ? 'Use the current mac2jb held FS session on macOS' :
    key === 'ns2jb' ? 'Start and supervise the pinned provider here' :
    'Physical Switch 1 Civ7 cartridge, version 1.0.0 · host-tested') +
    '</span></button>').join('');
  let out = heading('Set up for this boot',
    'Choose your entry method. The computer prepares the matched kit, then one explicit installation attempt starts the backend on the Switch.') +
    bootBanner() + picker + '<div class="method-list">' + list + '</div><div class="eyeline"><span class="tag">' +
    safe(osLabel()) + '</span><span class="tag">' + (allowed ? 'Supported host' : 'Unavailable on this host') +
    '</span></div>' + note('For this computer', osDirections(selected), allowed ? '' : 'error');
  if (!allowed) return out + help('Choose a supported host for this method. Native Windows is not a backend host; Windows uses a selected WSL2 distribution with Linux tools and paths.');
  if (selected === 'seedsow') out += seedsowSteps(ready);
  else if (selected === 'mac2jb') out += mac2jbSteps(ready);
  else if (selected === 'ns2jb') out += directSteps(ready, false);
  else out += directSteps(ready, true);
  return out + operationView() + help('A launcher connection is not verified FS access. Installer completion is not proof that an enabled mod changed a game. After setup, check Runtime details in Moader and then test the affected game.');
}
function prepareStep(ready) {
  const s = snapshot.system;
  const missing = [!s.git && 'Git', method === 'seedsow' && !s.clang && 'clang',
    method === 'seedsow' && s.os === 'linux' && !s.zstd && 'zstd'].filter(Boolean);
  const handoff = missing.length ? note('Computer tool needed',
    'Setup needs ' + safe(missing.join(', ')) + ' for this method. ' +
    (s.os === 'darwin' ? 'Open the macOS tools installer, finish its prompts, then return here.' :
      s.wsl ? 'Install them into the selected WSL2 distribution through its signed package manager. This changes that distribution’s system packages; no Switch action starts.' :
        'Install them through the signed Linux package manager. A graphical administrator prompt may appear. No Switch action starts.'),
    'warning') + '<div class="button-row">' +
    (s.os === 'darwin' ? button('Open macOS tools installer', 'dependency-handoff', 'secondary') :
      button(s.wsl ? 'Install tools in WSL2' : 'Install missing Linux tools',
        'install-host-tools', 'secondary') + (s.wsl ? '' :
        button('Open Linux Software app', 'dependency-handoff', 'quiet'))) +
    '</div><p class="hint">After the tools are available, choose Prepare and verify kit.</p>' : '';
  return '<section class="step"><div class="step-head"><h2>Prepare this computer</h2><small>' +
    (ready ? 'Ready' : 'Not prepared') + '</small></div><p>The kit checks its version, exact binaries and pinned provider source. Downloads and snapshots stay on this computer. This does not install on the Switch.</p>' +
    handoff +
    '<div class="button-row">' + button(ready ? 'Check preparation again' : 'Prepare and verify kit',
      'prepare', 'primary', missing.length > 0) +
    '</div></section>';
}
function seedsowSteps(ready) {
  const checked = snapshot.launcher?.online === true &&
    snapshot.launcher?.address === draft.console &&
    snapshot.launcher?.edition === draft.edition;
  const launcherNote = checked ? note('Launcher replied at ' +
    safe(new Date(snapshot.launcher.at).toLocaleTimeString()),
    'The selected ' + (draft.edition === 'demo' ? 'Demo' : 'full-game') +
    ' address replied. The launcher cannot identify the active game/profile or prove FS access. Setup checks it again when installation starts.',
    'success') : snapshot.launcher?.address === draft.console &&
    snapshot.launcher?.edition === draft.edition && snapshot.launcher?.error ?
    note('Launcher check failed', safe(snapshot.launcher.error) +
      ' Check the matching edition/profile and current address, then try again.', 'warning') :
    note('Launcher not checked for this selection',
      'Open the matching Seedsow edition and profile, press Y, then check its current address. The installer will also check it again.',
      'warning');
  let out = '<section class="step"><h2>Choose the Seedsow edition</h2><p>Use a working launcher save made for the same edition and the intended Switch profile. A Demo save is not a full-game save.</p>' +
    '<label class="field"><span>Edition</span><select data-draft="edition"><option value="demo"' +
    (draft.edition === 'demo' ? ' selected' : '') + '>Seedsow Demo</option><option value="full"' +
    (draft.edition === 'full' ? ' selected' : '') + '>Seedsow full game</option></select></label></section>';
  out += prepareStep(ready);
  out += '<section class="step"><h2>Connect the current launcher</h2><p>Fully reboot the Switch, open the selected Seedsow edition with its matching profile and press Y. Keep Seedsow open until setup says the listener has closed.</p>' +
    field('Switch LAN IPv4', 'console', 'For example, the address shown on your Switch',
      'The computer checks only the launcher first; this is not an FS check.') +
    '<div class="button-row">' + button('Check launcher connection', 'check-launcher', 'secondary', !ready) +
    '</div>' + launcherNote + '</section>';
  out += '<section class="step"><h2>Review and install once</h2><p>This attempt performs one qualified FS acquisition, installs the matched Moader backend including app and resident files, starts HEN, checks the service, and retires its owned listener. It may take time with no percentage shown.</p>' +
    (checked ? '<p class="hint">The launcher last replied for this selection. Setup will check it again.</p>' :
      '<p class="hint">The current launcher has not replied here yet. Check its address above.</p>') +
    check('gameReady', 'I fully rebooted, opened the correct edition and profile, pressed Y, and have not used another backend on this boot.') +
    '<div class="button-row">' + button('Install Moader on this boot', 'seedsow-install', 'primary',
      !ready || !draft.gameReady) + '</div></section>' +
    note('After the final completion', 'Press HOME and close Seedsow. Hold R while starting a native Switch 2 game, let it load, close it, then hold R to enter hbmenu. Open Moader and check Settings → Advanced → Runtime details. Repeat setup after a full reboot.');
  return out;
}
function mac2jbSteps(ready) {
  const selected = snapshot.selectedSession?.method === 'mac2jb';
  return prepareStep(ready) + '<section class="step"><h2>Use the current mac2jb session</h2><p>Follow the official mac2jb graphical and console steps until its FS session is verified for this boot. Keep SmileBASIC and mac2jb open. Paste the entire four-part status command; Moader reads its paths as data and checks the exact Node and provider.</p>' +
    '<label class="field"><span>Current status command</span><textarea data-draft="statusCommand" placeholder="Paste the complete command printed by mac2jb">' +
    safe(draft.statusCommand) + '</textarea></label><div class="button-row">' +
    button('Check session and prepare installer', 'prepare-held', 'primary', !ready) +
    '</div></section><section class="step"><h2>Review and install once</h2>' +
    (selected ? '<p>Qualified run: <span class="mono">' + safe(snapshot.selectedSession.run) + '</span>. The prepared snapshot is unique to this held session.</p>' :
      '<p>First prepare a verified held session above. The installer will refuse stale, mismatched or uncertain runs.</p>') +
    check('gameReady', 'SmileBASIC and its current host are still open, and this is the first Moader install attempt on this boot.') +
    '<div class="button-row">' + button('Install Moader on this boot', 'install-held', 'primary',
      !selected || !draft.gameReady) + '</div></section>' +
    note('After completion', 'When the installer finishes, press HOME and close SmileBASIC. Use hold R to enter hbmenu, open Moader and check the runtime. Repeat backend setup after a full reboot.');
}
function directSteps(ready, civ) {
  const host = snapshot.host?.method === method ? snapshot.host : null;
  const selected = snapshot.selectedSession?.method === method;
  let pre = '';
  if (civ) pre = note('Experimental requirements', 'Use the physical Switch 1 edition Civilization VII cartridge at game version 1.0.0. The admitted s8n host revision reports firmware 23.0.1; lower firmware and other forks are not qualified. The loader waiting screen is not FS access.', 'warning') +
    check('civCartridge', 'I checked the physical Switch 1 cartridge and version 1.0.0.') +
    check('civFirmware', 'I checked firmware 23.0.1 and the upstream entry requirements.') +
    check('civLoader', 'The CivSwitch loader is on its Awaiting payload screen.');
  else pre = note('Before starting', 'Complete the pinned upstream SmileBASIC entry preparation. The host will show the remaining Switch instructions. Keep SmileBASIC and this computer awake until installation finishes.');
  const addressLabel = civ ? 'Switch LAN IPv4' : 'This computer’s LAN IPv4';
  const addressKey = civ ? 'console' : 'hostAddress';
  const addressSuggestions = civ ? '' : '<div class="address-suggestions"><span>Detected on this computer:</span> ' +
    ((snapshot.hostAddresses || []).length ? snapshot.hostAddresses.map(address =>
      '<button type="button" class="address-choice" data-address="' + safe(address) + '">' +
      safe(address) + '</button>').join(' ') :
      '<span class="hint">No private LAN address found. Connect this computer to the Switch network first.</span>') +
    '</div>';
  return prepareStep(ready) + '<section class="step"><h2>Start the verified held session</h2>' +
    pre + field(addressLabel, addressKey, 'Private LAN IPv4',
      civ ? 'The host connects to the Switch loader on TCP 9026.' :
      'Choose a detected address that the Switch can reach. The provider checks whether it can bind TCP 39380.') +
    addressSuggestions +
    '<div class="button-row">' + button('Start and supervise provider', 'start-host', 'primary',
      !ready || !!host || civ && !(draft.civCartridge && draft.civFirmware && draft.civLoader)) +
    '</div>' + (host ? note(host.status === 'held' ? 'Verified held session' : 'Provider state',
      host.status === 'held' ? 'The pinned provider has a qualified run. Keep this host and the game open.' :
      'The provider is ' + safe(host.status) + '. Watch the waiting message and advanced events.') : '') +
    '</section><section class="step"><h2>Prepare Moader for that session</h2><p>Moader verifies the provider family, run evidence and matched files before making a unique snapshot. This is computer preparation.</p><div class="button-row">' +
    button('Check held session and prepare installer', 'prepare-held', 'primary',
      !host || host.status !== 'held') + '</div></section><section class="step"><h2>Review and install once</h2>' +
    (selected ? '<p>Qualified run: <span class="mono">' + safe(snapshot.selectedSession.run) + '</span></p>' :
      '<p>Prepare the held session above first.</p>') +
    check('gameReady', 'The original provider and game remain open, and this boot has not used a Moader installer attempt.') +
    '<div class="button-row">' + button('Install Moader on this boot', 'install-held', 'primary',
      !selected || !draft.gameReady || host?.status !== 'held') + '</div></section>' +
    note('After completion', civ ?
      'The host result still needs a console check. Close Civilization VII after the host finishes, enter hbmenu and check Moader. This experimental route has no Moader hardware acceptance yet.' :
      'When the installer finishes, press HOME and close SmileBASIC. Enter hbmenu with hold R and check Moader’s runtime. Repeat setup after a full reboot.');
}
function standalone() {
  const captures = snapshot.captures || [], selected = !!snapshot.selectedCapture;
  const payloadReady = snapshot.standaloneReady === true;
  const plan = snapshot.sdPlan?.kind === 'standalone' ? snapshot.sdPlan : null;
  return heading('Prepare computer-free boots',
    'This is the Seedsow on-console A/B route. A needs one exact qualified sparse FS capture from a completed PC-driven installation. The public ZIP alone cannot make it.') +
    bootBanner() + note('A and B', 'A installs Moader’s FS path. B skips that adapter. Both use the staged native files. On a fresh boot, choose one route; do not combine it with a PC-driven attempt.', 'warning') +
    '<section class="step"><h2>Find the eligible capture</h2><p>Moader checks the adjacent qualification manifest, exact generator fingerprint and completed PC-driven receipts. A file left behind before validation finished is not enough.</p>' +
    (captures.length ? '<div class="capture-list">' + captures.map((x, index) =>
      '<div class="capture-row"><p><strong>' + (x.eligible ? 'Eligible capture' : 'Not accepted') +
      '</strong><br><span class="hint mono">' + safe(x.file) + '</span>' +
      (x.reason ? '<br><span class="hint">' + safe(x.reason) + '</span>' : '') +
      '</p>' + (x.eligible ? button('Use capture', 'select-capture:' + index, 'secondary') : '') +
      '</div>').join('') + '</div>' : note('No capture found in this kit', 'Run the PC-driven Seedsow setup successfully first, or import the capture, manifest and completed receipts from that earlier kit. A different capture fingerprint has no supported A conversion.')) +
    '<label class="field"><span>Import from an earlier completed kit</span><input id="captureFiles" type="file" multiple><small>Select fs-4.bin, manifest.json, moader-install.json, moader-success-retirement.json and moader-device-verification-pending.json. Files stay on this computer.</small></label>' +
    '<div class="button-row">' + button('Import and check selected files', 'import-capture', 'secondary') +
    button('Generate and check A/B files', 'generate-standalone', 'primary', !selected) +
    '</div></section><section class="step"><h2>Stage the mounted SD card</h2><p>Choose the SD root in the browser’s folder picker. Review the actual file list, then write and read back every file. Existing files are copied to a backup folder on the card first.</p>' +
    (payloadReady ? note('A/B payload checked', 'The qualified local payload is ready to stage. This is still computer preparation.', 'success') :
      note('Generate first', 'The SD list needs the checked A/B payload. Select an eligible capture and complete Generate and check A/B files above.', 'warning')) +
    '<div class="button-row">' + button('Prepare SD file list', 'export-standalone', 'primary', !payloadReady) +
    button('Choose mounted SD card', 'pick-sd', 'secondary', !plan) + '</div>' + sdPreview(plan) +
    '</section><section class="step"><h2>Write the confirmed profile save</h2><p>The uploader changes the save of the profile currently running Seedsow. The Switch address does not identify that profile. Back up saves separately before this step; the on-card file backup above does not back up a console save.</p>' +
    '<label class="field"><span>Active edition</span><select data-draft="edition"><option value="demo"' +
    (draft.edition === 'demo' ? ' selected' : '') + '>Seedsow Demo</option><option value="full"' +
    (draft.edition === 'full' ? ' selected' : '') + '>Seedsow full game</option></select><small>Confirm this on the Switch. The address cannot identify the edition or profile.</small></label>' +
    check('profileConfirmed', 'I confirmed the intended Seedsow edition and active profile on the Switch.') +
    check('backupConfirmed', 'I made and checked a separate backup of any save I need to keep.') +
    field('Current Switch LAN IPv4', 'console', 'Switch address after pressing Y') +
    '<div class="button-row">' + button('Upload standalone save', 'upload-standalone', 'primary',
      !plan || !snapshot.sdResult?.complete || !draft.profileConfirmed || !draft.backupConfirmed) +
    '</div></section><section class="step"><h2>Verify after a fresh reboot</h2><p>Fully reboot, open Seedsow on the confirmed profile and press Y. Check that the saved modules persisted, then close and reopen Seedsow before choosing A or B.</p>' +
    '<div class="button-row">' + button('Check saved modules', 'verify-standalone-save', 'secondary') +
    '</div></section>' + operationView() +
    help('The standalone generator accepts one pinned sparse FS digest. A completed PC-driven setup can still produce a different capture. If rejected, use the PC-driven method. Do not replace another profile’s save or delete scratch entries.');
}
function sdPreview(plan) {
  if (!plan) return '';
  let html = note('SD preview', safe(plan.files.length) + ' checked files · ' +
    safe((plan.totalBytes / 1024 / 1024).toFixed(1)) + ' MiB. Destination: the selected card root. The file list uses paths such as <span class="mono">switch/Moader.nro</span> and <span class="mono">kaihen/plugins/moader.nso</span>.');
  html += '<details class="file-preview"><summary>Review all destination files</summary><ul class="compact-list">' +
    plan.files.map(row => '<li><span class="mono">' + safe(row.path) + '</span> · ' +
      safe((row.bytes / 1024).toFixed(0)) + ' KiB' +
      (sdHandle ? sdExisting.includes(row.path) ?
        ' · existing file backed up before replacement' : ' · new file' : '') +
      '</li>').join('') + '</ul></details>';
  if (sdHandle) html += note('Selected folder', '<span class="mono">' + safe(sdHandle.name) +
    '</span>. ' + safe(sdExisting.length) + ' planned files already exist and will be backed up on the card. Verify this is the mounted Switch SD root before writing.') +
    '<div class="button-row">' + button('Write and verify ' + plan.files.length + ' files', 'write-sd') + '</div>';
  if (snapshot.sdResult) html += note(snapshot.sdResult.complete ? 'Readback complete' : 'Readback incomplete',
    safe(snapshot.sdResult.verified) + ' of ' + safe(snapshot.sdResult.total) +
    ' files verified on the selected card. ' +
    (snapshot.sdResult.error ? '<strong>First error:</strong> ' + safe(snapshot.sdResult.error) + '. ' : '') +
    (snapshot.sdResult.complete ? 'Eject the card safely before returning it to the Switch.' :
      'The card may contain a partial update. Keep the backup and retry staging only after checking the card.'),
    snapshot.sdResult.complete ? 'success' : 'warning');
  return html;
}
function copy() {
  const plan = snapshot.sdPlan?.kind === 'app' ? snapshot.sdPlan : null;
  return heading('Copy app and plugin files',
    'Use the two exact files from this backend kit. Copying places them on SD; it does not start HEN, install FS for a fresh boot, or replace a service already running in memory.') +
    '<dl class="facts"><dt>Console app</dt><dd><span class="mono">/switch/Moader.nro</span></dd>' +
    '<dt>Resident plugin</dt><dd><span class="mono">/kaihen/plugins/moader.nso</span></dd></dl>' +
    '<div class="button-row">' + button('Prepare the matched files', 'export-app') +
    button('Choose mounted SD card', 'pick-sd', 'secondary', !plan) + '</div>' +
    sdPreview(plan) + note('After copying', 'Close Moader before replacing its files. If a compatible resident is already running, use Settings → Advanced → Reload Moader service in the console app. A fresh boot still needs backend setup.') +
    operationView() + help('The complete backend installer already includes these app and plugin files. Use this task when updating an existing compatible backend or preparing an SD card separately.');
}
function reload() {
  return heading('Update a running service',
    'A compatible reload replaces the resident already running on this boot. Copying a plugin file alone does not change that process.') +
    '<ol class="compact-list"><li>Finish uploads and close the Moader app.</li><li>Use Copy app and plugin to put the matched files on SD.</li><li>Open Moader from hbmenu and choose Settings → Advanced → Reload Moader service.</li><li>Keep the app open until it reconnects or displays a failure.</li><li>Reopen the browser page. If its listener is off, press ZR and start it.</li></ol>' +
    note('If reload is unavailable', 'The running HEN may not support this checked protocol. Use the matching backend setup for a new boot. If a reload times out or is quarantined, keep its diagnostics and avoid a competing operation.', 'warning') +
    '<div class="button-row">' + button('Copy matched files', 'go-copy', 'secondary') +
    button('Check connection', 'go-connection', 'secondary') + '</div>' +
    help('The reload command is inside the console app. There is no supported computer-side endpoint for submitting it. The app uses HEN operation status and startup checks; keep it open until the result appears.');
}
function connection() {
  const s = snapshot.system;
  return heading('Check connection and setup state',
    'The computer can check this kit and a reachable launcher. The Switch app reports whether the current runtime and filesystem are connected.') +
    '<dl class="facts"><dt>Kit</dt><dd>' + (snapshot.kit.ready ? 'Matched and complete' :
      'Missing or mismatched files: ' + safe(snapshot.kit.missing.concat(snapshot.kit.wrong).join(', '))) +
    '</dd><dt>Computer</dt><dd>' + safe(osLabel()) + '</dd><dt>Node</dt><dd>' +
    safe(s.node) + '</dd><dt>Python</dt><dd>' + safe(s.python || 'Missing') + '</dd><dt>Last setup task</dt><dd>' +
    safe(snapshot.operation?.phase || 'Not started') + '</dd></dl>' +
    field('Switch LAN IPv4', 'console', 'Current address shown by the Switch') +
    '<div class="button-row">' + button('Check Seedsow launcher', 'check-launcher', 'secondary') +
    button('Open Moader browser page', 'go-browser', 'secondary') + '</div>' +
    note('Check on the Switch', 'Open Moader → Settings → Advanced → Runtime details. Look for Connected and check the FS acknowledgement for the current service and routing generation. A reachable page or a launcher response alone does not prove that a mod changed a game.') +
    operationView() + help('The resident can keep the browser available after the foreground app closes. Foreground browser access only lasts while the app is open. After a reboot, the backend must be set up again.');
}
function browser() {
  return heading('Open Moader in browser',
    'Use the current HTTP address shown under Connection in the Switch app. This opens the Switch-hosted library, separate from the Moader Setup app.') +
    field('Switch LAN IPv4', 'console', 'Address shown in Moader') +
    field('Listener port', 'listenerPort', 'Port shown in Moader', 'Use the current port shown by the app, especially after changing it.', 'number') +
    '<div class="button-row">' + button('Open Moader in browser', 'open-browser') + '</div>' +
    note('Pairing', 'Enter the six digits shown on the Switch, including any leading zeros. Keep the code private. If the page stops after closing Moader, check whether a matched resident service is running for this boot.') +
    help('Press ZR in the console app to check the listener and its exact address. A saved port preference can take effect only after restarting that listener.');
}
function report() {
  return heading('Export support report',
    'Save a readable summary or an advanced report from this computer. Nothing is uploaded automatically.') +
    '<dl class="facts"><dt>Version</dt><dd>' + safe(snapshot.kit.version) +
    '</dd><dt>Current stage</dt><dd>' + safe(snapshot.operation?.phase || 'Not started') +
    '</dd><dt>First error</dt><dd>' + safe(snapshot.operation?.firstError || 'None recorded') +
    '</dd><dt>Native outcome</dt><dd>' +
    safe(snapshot.uncertain ? 'Uncertain · retain evidence' :
      snapshot.operation?.nativeStarted ? snapshot.operation.status : 'Never started') +
    '</dd></dl>' +
    note('Included', 'Version and component digests, host and method, setup events, the first meaningful error, and small relevant run receipts. The advanced JSON includes redacted diagnostic excerpts.') +
    note('Excluded by default', 'Pairing codes, credentials, saves, game files and binary device-memory captures. Address and path redaction is best effort, so review the saved report before sharing.', 'warning') +
    '<div class="button-row">' + button('Save readable summary', 'download-text') +
    button('Save advanced report', 'download-json', 'secondary') + '</div>' +
    help('The original run journals stay in place. This export is a separate support copy; it does not delete evidence or contact a support service.');
}
const views = {home, boot, standalone, copy, reload, connection, browser, report};
function render() {
  if (!snapshot) return;
  const focused = main.contains(document.activeElement) ? document.activeElement : null;
  const focusKey = focused && {id: focused.id, draft: focused.dataset.draft,
    action: focused.dataset.action, method: focused.dataset.method,
    address: focused.dataset.address,
    selection: typeof focused.selectionStart === 'number' ? focused.selectionStart : null};
  const pageChanged = main.dataset.page !== task;
  main.dataset.page = task;
  main.innerHTML = '<div class="content">' + views[task]() + '</div>';
  if (pageChanged) main.scrollTop = 0;
  if (focusKey) {
    const matching = [...main.querySelectorAll('button,input,select,textarea,summary')].find(item =>
      focusKey.id && item.id === focusKey.id ||
      focusKey.draft && item.dataset.draft === focusKey.draft ||
      focusKey.action && item.dataset.action === focusKey.action ||
      focusKey.method && item.dataset.method === focusKey.method ||
      focusKey.address && item.dataset.address === focusKey.address);
    (matching && !matching.disabled ? matching : main).focus({preventScroll: true});
    if (matching && !matching.disabled && focusKey.selection !== null &&
        typeof matching.setSelectionRange === 'function')
      matching.setSelectionRange(focusKey.selection, focusKey.selection);
  }
  document.querySelectorAll('.nav-item').forEach(item => {
    item.classList.toggle('current', item.dataset.task === task);
    item.setAttribute('aria-current', item.dataset.task === task ? 'page' : 'false');
  });
  document.getElementById('taskSelect').value = task;
  document.getElementById('kitBadge').textContent = snapshot.kit.ready ?
    'Kit ' + snapshot.kit.version + ' verified' : 'Kit needs attention';
  document.getElementById('kitBadge').className = 'badge ' +
    (snapshot.kit.ready ? 'good' : 'bad');
  document.getElementById('platformLabel').textContent = osLabel();
}
function activity() {
  if (!snapshot) return;
  const sd = latestSdResult();
  const op = snapshot.operation;
  const route = document.getElementById('activityRoute');
  let title, message, labelText, labelClass, sourceTask = null;
  if (sd) {
    sourceTask = sd.kind === 'app' ? 'copy' : 'standalone';
    title = sd.complete ? 'SD files written and checked' : 'SD staging incomplete';
    message = sd.complete ?
      'The selected card was read back. Eject it safely before using it on the Switch.' :
      (sd.error || 'A write or readback stopped.') + ' Inspect the card and keep its backup.';
    labelText = sd.complete ? 'readback complete' : 'readback incomplete';
    labelClass = sd.complete ? 'good' : 'bad';
  } else {
    sourceTask = op && operationTask(op);
    title = op ?
    (op.status === 'running' ? 'Working on ' : op.status === 'complete' ? 'Completed ' :
      op.status === 'uncertain' ? 'Outcome uncertain · ' : 'Stopped · ') +
      op.action.replaceAll('-', ' ') : 'No task running';
    message = op?.firstError || op?.message || '';
    labelText = op?.phase.replaceAll('-', ' ') || '';
    labelClass = !op ? '' : op.status === 'complete' ? 'good' :
    op.status === 'uncertain' || op.status === 'failed' ? 'bad' : 'wait';
  }
  if (sourceTask && task !== sourceTask && task !== 'home') {
    title = 'Recent task · ' + taskNames[sourceTask];
    route.hidden = false;
    route.dataset.task = sourceTask;
    route.textContent = 'Open ' + taskNames[sourceTask];
  } else {
    route.hidden = true;
    delete route.dataset.task;
  }
  document.getElementById('activityTitle').textContent = title;
  document.getElementById('activityMessage').textContent = message;
  const state = document.getElementById('activityState');
  state.textContent = labelText;
  state.className = 'state-label ' + labelClass;
  const events = snapshot.events || [];
  document.getElementById('eventList').innerHTML = events.slice(-45).reverse().map(row =>
    '<div class="event"><time>' + safe(new Date(row.at).toLocaleTimeString()) +
    '</time><span>' + safe(row.message) + '</span></div>').join('') ||
    '<p class="hint">No setup events yet.</p>';
}
async function refresh(force = false) {
  try {
    const data = await (await api('/api/state')).json();
    const before = snapshot?.operation;
    snapshot = data;
    if (lastPlanId && data.sdPlan?.id !== lastPlanId) {
      sdHandle = null; sdExisting = [];
    }
    lastPlanId = data.sdPlan?.id || null;
    if (!draft.console && data.prefs.console) draft.console = data.prefs.console;
    if (!draft.listenerPort) draft.listenerPort = data.prefs.listenerPort || 31337;
    const key = JSON.stringify([task, method, data.operation?.id, data.operation?.phase,
      data.operation?.status, data.prepared, data.selectedSession, data.selectedCapture,
      data.host, data.sdPlan?.at, data.sdResult, data.uncertain, data.bootAttempts,
      data.standaloneReady, data.launcher, data.system]);
    if (force || key !== renderKey) { renderKey = key; render(); }
    activity();
    if (initialRefreshDone && data.operation &&
        data.operation.status !== 'running' &&
        data.operation.id !== lastAnnouncedOperation &&
        (before?.id !== data.operation.id || before.status === 'running')) {
      announcer.textContent = data.operation.status === 'complete' ?
        'Setup task completed. ' + data.operation.message :
        'Setup task stopped. ' + (data.operation.firstError || data.operation.message);
      lastAnnouncedOperation = data.operation.id;
    }
    initialRefreshDone = true;
  } catch (error) { show(error.message); }
}
async function savePrefs() {
  await post('/api/preferences', {lastMethod: method, console: draft.console || '',
    listenerPort: Number(draft.listenerPort) || 31337});
}
async function begin(value) {
  await post('/api/operation', value);
  await refresh(true);
}
async function download(format) {
  const result = await api('/api/report?format=' + format);
  const blob = await result.blob();
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = 'Moader-Setup-support.' + (format === 'text' ? 'txt' : 'json');
  document.body.append(link); link.click(); link.remove();
  setTimeout(() => URL.revokeObjectURL(link.href), 30000);
  show('The support copy is ready. Review it before sharing.');
}
async function importCapture() {
  const input = document.getElementById('captureFiles');
  const files = [...input.files];
  const names = ['fs-4.bin', 'manifest.json', 'moader-install.json',
    'moader-success-retirement.json', 'moader-device-verification-pending.json'];
  if (files.length !== names.length || files.some(x => !names.includes(x.name)))
    throw Error('Select the five named files shown here from one completed run.');
  const encoded = {};
  for (const file of files) {
    if (file.size > (file.name === 'fs-4.bin' ? 2416640 : 250000))
      throw Error('One selected file is larger than the expected bounded report.');
    const bytes = new Uint8Array(await file.arrayBuffer());
    let binary = '';
    for (let at = 0; at < bytes.length; at += 32768)
      binary += String.fromCharCode(...bytes.subarray(at, at + 32768));
    encoded[file.name] = btoa(binary);
  }
  await post('/api/capture/import', {files: encoded});
  show('The imported capture and completed receipts passed their checks.');
  await refresh(true);
}
async function pickSd() {
  if (!window.showDirectoryPicker) throw Error('The desktop app could not open a writable folder picker on this computer. Reopen Moader Setup and try again.');
  let handle;
  try { handle = await window.showDirectoryPicker({mode: 'readwrite'}); }
  catch (error) {
    if (error?.name === 'AbortError') {
      show('SD card selection was canceled. No files were changed.');
      return;
    }
    throw error;
  }
  let nintendo = false, layout = false;
  try { await handle.getDirectoryHandle('Nintendo'); nintendo = true; } catch {}
  try { await handle.getDirectoryHandle('switch'); await handle.getDirectoryHandle('kaihen'); layout = true; } catch {}
  if (!nintendo && !layout) throw Error('This folder does not look like the Switch SD root. Choose the mounted card’s top-level folder, not a folder inside it.');
  sdHandle = handle;
  sdExisting = [];
  for (const row of snapshot.sdPlan.files) {
    try {
      let folder = handle;
      const parts = row.path.split('/');
      for (const part of parts.slice(0, -1)) folder = await folder.getDirectoryHandle(part);
      await folder.getFileHandle(parts.at(-1));
      sdExisting.push(row.path);
    } catch {}
  }
  render();
  show('Review the selected card name and file list, then choose Write and verify.');
}
async function writeSd() {
  const plan = snapshot.sdPlan;
  if (!sdHandle || !plan) throw Error('Choose the mounted SD root and prepare the file list first.');
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  let count = 0;
  try {
    for (let index = 0; index < plan.files.length; index++) {
      const row = plan.files[index];
      const parts = row.path.split('/');
      if (parts.some(x => !x || x === '.' || x === '..')) throw Error('Invalid SD file path.');
      const bytes = await (await api('/api/stage-file?index=' + index)).arrayBuffer();
      const sourceHash = [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))]
        .map(x => x.toString(16).padStart(2, '0')).join('');
      if (sourceHash !== row.sha256 || bytes.byteLength !== row.bytes)
        throw Error('The file source changed before staging: ' + row.path);
      let folder = sdHandle;
      for (const part of parts.slice(0, -1)) folder = await folder.getDirectoryHandle(part, {create: true});
      let old = null;
      try { old = await folder.getFileHandle(parts.at(-1)); } catch {}
      if (old) {
        const oldBytes = await (await old.getFile()).arrayBuffer();
        let backup = await sdHandle.getDirectoryHandle('moader-setup-backups', {create: true});
        backup = await backup.getDirectoryHandle(stamp, {create: true});
        for (const part of parts.slice(0, -1)) backup = await backup.getDirectoryHandle(part, {create: true});
        const backupFile = await backup.getFileHandle(parts.at(-1), {create: true});
        const backupWriter = await backupFile.createWritable();
        await backupWriter.write(oldBytes); await backupWriter.close();
        const backed = new Uint8Array(await (await backupFile.getFile()).arrayBuffer());
        const original = new Uint8Array(oldBytes);
        if (backed.byteLength !== original.byteLength ||
            !backed.every((byte, index) => byte === original[index]))
          throw Error('The existing SD file backup did not read back: ' + row.path);
      }
      const target = await folder.getFileHandle(parts.at(-1), {create: true});
      const writer = await target.createWritable();
      await writer.write(bytes); await writer.close();
      const checked = await (await target.getFile()).arrayBuffer();
      const digest = [...new Uint8Array(await crypto.subtle.digest('SHA-256', checked))]
        .map(x => x.toString(16).padStart(2, '0')).join('');
      if (digest !== row.sha256) throw Error('SD readback differs: ' + row.path);
      count++;
      announcer.textContent = 'Verified ' + count + ' of ' + plan.files.length + ' SD files.';
    }
    await post('/api/stage-result', {planId: plan.id, kind: plan.kind,
      verified: count, complete: true});
    show('All ' + count + ' files were written and read back. Eject the card safely.');
  } catch (error) {
    await post('/api/stage-result', {planId: plan.id, kind: plan.kind,
      verified: count, complete: false, error: error.message});
    await refresh(true);
    throw Error(error.message + ' The card may contain a partial update; keep its backup.');
  }
  await refresh(true);
}
async function action(name) {
  const busy = snapshot.operation?.status === 'running';
  if (busy && !['check-launcher', 'download-text', 'download-json'].includes(name))
    throw Error('Wait for the current setup operation to finish.');
  if (name === 'prepare') return begin({action: 'prepare', method});
  if (name === 'install-host-tools') return begin({action: name, method});
  if (name === 'dependency-handoff') {
    const result = await post('/api/dependency-handoff', {});
    show(result.message); return refresh(true);
  }
  if (name === 'check-launcher') {
    const result = await post('/api/check-launcher', {address: draft.console,
      edition: draft.edition});
    show(result.message); return refresh(true);
  }
  if (name === 'seedsow-install') {
    if (!draft.gameReady) throw Error('Confirm the fresh boot and matching launcher first.');
    return begin({action: name, address: draft.console, edition: draft.edition});
  }
  if (name === 'start-host') return begin({action: name, method,
    address: method === 'civswitch' ? draft.console : draft.hostAddress});
  if (name === 'prepare-held') return begin({action: name, method,
    statusCommand: draft.statusCommand});
  if (name === 'install-held') {
    if (!draft.gameReady) throw Error('Confirm the current held session first.');
    return begin({action: name, method});
  }
  if (name.startsWith('select-capture:')) {
    await post('/api/capture/select', {index: Number(name.split(':')[1])});
    show('The eligible capture is selected.'); return refresh(true);
  }
  if (name === 'import-capture') return importCapture();
  if (name === 'generate-standalone') return begin({action: name});
  if (name === 'export-standalone') return begin({action: 'export-sd', kind: 'standalone'});
  if (name === 'export-app') return begin({action: 'export-sd', kind: 'app'});
  if (name === 'pick-sd') return pickSd();
  if (name === 'write-sd') return writeSd();
  if (name === 'upload-standalone') return begin({action: name, address: draft.console,
    edition: draft.edition,
    profileConfirmed: draft.profileConfirmed, backupConfirmed: draft.backupConfirmed});
  if (name === 'verify-standalone-save') return begin({action: name, address: draft.console});
  if (name === 'download-text') return download('text');
  if (name === 'download-json') return download('json');
  if (name === 'new-boot') {
    if (!draft.bootRebooted) throw Error('Confirm the full Switch reboot first.');
    await post('/api/new-boot', {confirmed: true});
    draft.bootRebooted = false; draft.gameReady = false;
    show('A new boot record is ready. Earlier attempt evidence was kept.');
    return refresh(true);
  }
  if (name === 'open-browser') {
    if (!/^(?:10|192|172|169)\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(draft.console))
      throw Error('Enter the current private Switch address shown by Moader.');
    const port = Number(draft.listenerPort);
    if (!Number.isInteger(port) || port < 1024 || port > 65535)
      throw Error('Enter the listener port shown by Moader.');
    await savePrefs();
    window.open('http://' + draft.console + ':' + port + '/', '_blank', 'noopener');
    return;
  }
  if (name.startsWith('go-')) {
    task = name.slice(3); render(); window.scrollTo({top: 0, behavior: 'instant'});
    main.focus({preventScroll: true}); return;
  }
}
document.addEventListener('click', async event => {
  const button = event.target.closest('button');
  if (!button) return;
  if (button.dataset.task) {
    task = button.dataset.task; sdHandle = null; sdExisting = []; renderKey = ''; render();
    window.scrollTo({top: 0, behavior: 'instant'});
    main.focus({preventScroll: true}); activity(); return;
  }
  if (button.dataset.method) {
    method = button.dataset.method; draft.gameReady = false; renderKey = '';
    await savePrefs().catch(() => {}); render(); return;
  }
  if (button.dataset.address) {
    draft.hostAddress = button.dataset.address;
    render(); return;
  }
  if (button.dataset.action) {
    button.disabled = true;
    try { await action(button.dataset.action); }
    catch (error) { show(error.message); }
    finally { if (button.isConnected) button.disabled = false; }
  }
});
document.addEventListener('input', event => {
  const key = event.target.dataset.draft;
  if (!key) return;
  draft[key] = event.target.type === 'checkbox' ? event.target.checked : event.target.value;
  if (key === 'gameReady' || key === 'bootRebooted' || key.startsWith('civ') ||
      key.endsWith('Confirmed')) render();
});
document.addEventListener('change', event => {
  if (event.target.id === 'taskSelect') {
    task = event.target.value; sdHandle = null; sdExisting = []; renderKey = ''; render();
    window.scrollTo({top: 0, behavior: 'instant'});
    main.focus({preventScroll: true}); activity(); return;
  }
  if (event.target.id === 'methodSelect') {
    method = event.target.value; draft.gameReady = false; renderKey = '';
    savePrefs().catch(() => {}); render(); return;
  }
  const key = event.target.dataset.draft;
  if (key === 'edition') { render(); return; }
  if (key === 'console' || key === 'listenerPort') {
    savePrefs().catch(() => {});
    if (key === 'console') render();
  }
});
document.getElementById('helpButton').addEventListener('click', () => {
  const target = document.getElementById('stepHelp');
  if (target) { target.open = true; target.scrollIntoView({block: 'nearest'}); }
  else show('The task instructions on this page are the current help.');
});
refresh(true).then(() => {
  if (!snapshot) {
    main.innerHTML = '<div class="content">' + heading('Setup is unavailable',
      'Close this window and reopen Moader Setup to reconnect to its local controller.') + '</div>';
    const kit = document.getElementById('kitBadge');
    kit.textContent = 'Kit status unavailable';
    kit.classList.add('bad');
    document.getElementById('platformLabel').textContent = 'Unavailable';
    document.getElementById('activity').hidden = true;
    document.getElementById('taskSelect').disabled = true;
    document.getElementById('helpButton').disabled = true;
    document.querySelectorAll('.nav-item').forEach(item => {
      item.disabled = true;
      item.classList.remove('current');
      item.removeAttribute('aria-current');
    });
    return;
  }
  method = snapshot.prefs.lastMethod || 'seedsow';
  render(); setInterval(() => refresh(), 1600);
});
