// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const net = require('node:net');
const {spawn} = require('node:child_process');
const {app, BrowserWindow, dialog, Menu, shell} = require('electron');

const kit = path.resolve(__dirname, '../..');
const logo = path.join(kit, 'branding/mark-alpha.png');
app.setName('Moader Setup');
if (process.platform === 'darwin') Menu.setApplicationMenu(Menu.buildFromTemplate([
  {label: 'Moader Setup', submenu: [{role: 'about'}, {type: 'separator'},
    {role: 'hide'}, {role: 'hideOthers'}, {role: 'unhide'}, {type: 'separator'}, {role: 'quit'}]},
  {label: 'Edit', submenu: [{role: 'undo'}, {role: 'redo'}, {type: 'separator'},
    {role: 'cut'}, {role: 'copy'}, {role: 'paste'}, {role: 'selectAll'}]},
  {label: 'Window', submenu: [{role: 'minimize'}, {role: 'zoom'}, {role: 'front'}]},
]));
let window = null;
let setupUrl = null;

function privateConsolePage(value) {
  try {
    const url = new URL(value);
    if (url.protocol !== 'http:' || net.isIP(url.hostname) !== 4) return false;
    const parts = url.hostname.split('.').map(Number);
    return parts[0] === 10 || parts[0] === 192 && parts[1] === 168 ||
      parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31 ||
      parts[0] === 169 && parts[1] === 254;
  } catch { return false; }
}
function verifiedLocalUrl(value) {
  const url = new URL(value);
  if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' ||
      !/^\d{1,5}$/.test(url.port) || url.pathname !== '/' ||
      !/^[a-f0-9]{64}$/.test(url.searchParams.get('token') || ''))
    throw Error('The setup controller returned an invalid local address.');
  return url;
}
async function checkController(value) {
  const url = verifiedLocalUrl(value);
  const response = await fetch(url.origin + '/health', {
    headers: {'X-Moader-Token': url.searchParams.get('token')},
    signal: AbortSignal.timeout(4000),
  });
  if (!response.ok) throw Error('The local setup controller did not answer.');
  return url;
}
function startController() {
  return new Promise((resolve, reject) => {
    const isWindows = process.platform === 'win32';
    const executable = isWindows ? 'powershell.exe' : '/bin/sh';
    const args = isWindows ? ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
      path.join(kit, 'setup/windows-launch.ps1'), kit] :
      [path.join(kit, 'setup/launch.sh'), kit];
    const child = spawn(executable, args, {cwd: kit, windowsHide: true,
      env: {...process.env, MOADER_SETUP_DETACH: '1', MOADER_SETUP_NO_OPEN: '1'},
      stdio: ['ignore', 'pipe', 'pipe']});
    let output = '', errorText = '', settled = false;
    const timer = setTimeout(() => finish(Error('The setup controller did not open in time. Check the extracted kit and reopen Moader Setup.')), 300000);
    function finish(error, url) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (error) reject(error); else resolve(url);
    }
    child.stdout.on('data', bytes => {
      output = (output + bytes.toString('utf8').replace(/\0/g, '')).slice(-16384);
      const match = /Moader Setup is ready at (http:\/\/127\.0\.0\.1:\d+\/\?token=[a-f0-9]{64})/.exec(output);
      if (match) finish(null, match[1]);
    });
    child.stderr.on('data', bytes => { errorText = (errorText + bytes.toString('utf8')).slice(-4000); });
    child.once('error', finish);
    child.once('exit', code => {
      if (!settled) finish(Error(errorText.trim() ||
        (code === 0 ? 'The setup launcher closed before the controller opened.' :
          'The setup launcher stopped before the controller opened.')));
    });
  });
}
function makeWindow() {
  window = new BrowserWindow({width: 1180, height: 790, minWidth: 380,
    minHeight: 500, title: 'Moader Setup', backgroundColor: '#101a15',
    icon: fs.existsSync(logo) ? logo : undefined, autoHideMenuBar: true,
    webPreferences: {nodeIntegration: false, contextIsolation: true,
      sandbox: true, webSecurity: true}});
  if (process.platform === 'darwin' && fs.existsSync(logo)) app.dock.setIcon(logo);
  window.webContents.setWindowOpenHandler(({url}) => {
    if (privateConsolePage(url)) shell.openExternal(url);
    return {action: 'deny'};
  });
  window.webContents.on('will-navigate', (event, url) => {
    if (!setupUrl || !url.startsWith(setupUrl.origin + '/')) event.preventDefault();
  });
  window.webContents.session.setPermissionCheckHandler((contents, permission, origin) =>
    permission === 'fileSystem' && contents === window.webContents &&
      !!setupUrl && origin === setupUrl.origin);
  window.webContents.session.setPermissionRequestHandler((contents, permission, callback, details) => {
    let allowed = false;
    try { allowed = permission === 'fileSystem' && contents === window.webContents &&
      !!setupUrl && new URL(details.requestingUrl || contents.getURL()).origin === setupUrl.origin; }
    catch { /* Reject malformed origins. */ }
    callback(allowed);
  });
  window.webContents.session.on('will-download', (event, item) => {
    if (!setupUrl || item.getInitiatorOrigin() !== setupUrl.origin ||
        !/^Moader-Setup-support\.(?:txt|json)$/.test(item.getFilename())) {
      event.preventDefault(); return;
    }
    item.setSaveDialogOptions({title: 'Save Moader Setup report',
      defaultPath: item.getFilename()});
  });
  window.on('closed', () => { window = null; });
  window.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(
    '<html><body style="background:#101a15;color:#eff8ed;font:18px system-ui;padding:11vw"><h1>Opening Moader Setup</h1><p>Checking the extracted kit and starting the local controller…</p></body></html>'));
  return window;
}
async function openSetup() {
  const shown = window || makeWindow();
  if (window && setupUrl) { shown.focus(); return; }
  try {
    setupUrl = await checkController(await startController());
    await shown.loadURL(setupUrl.toString());
  } catch (error) {
    dialog.showErrorBox('Moader Setup could not open', String(error.message || error));
    app.quit();
  }
}
app.whenReady().then(openSetup);
app.on('activate', () => { if (!window) openSetup(); else window.focus(); });
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
