// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// One request per owned Seedsow retained-endpoint socket. The pinned endpoint
// serves a JSON line and then closes. A lost reply never retries, retires the
// acquisition, closes the socket, or releases its producer's ownership.
const fs = require('node:fs');
const path = require('node:path');
const net = require('node:net');
const crypto = require('node:crypto');
const check = (ok, message) => { if (!ok) throw Error(message); };
const MAX_REQUEST = 0x100000, MAX_REPLY = 16 * 1024 * 1024;
const ALLOWED = new Set([
  'info', 'retained-info', 'retained-read', 'retained-write', 'retained-inject-holder',
  'retained-holder-probe', 'retained-install', 'retained-bootstrap-move',
  'retained-bootstrap-read', 'read', 'write', 'root-open', 'root-initialize',
  'root-clones',
  'root-native-info', 'root-native-ipc', 'root-native-domain-open', 'root-native-domain-copy',
  'root-native-close-handle', 'root-native-sm-service', 'root-native-code-open',
  'root-native-code-domain', 'root-native-file-capture', 'root-native-file-stage',
  'root-native-file-package-row', 'root-native-package-stage', 'root-native-package-open',
  'root-native-donor-prepare', 'root-native-file-main-swap',
  'root-native-file-code-swap', 'root-native-file-fs-swap',
  'root-native-notifier-open', 'root-native-notifier-clones', 'root-native-notifier-event',
  'root-native-locator-open', 'root-native-event-signal', 'root-native-event-clear',
  'root-native-wait-zero', 'root-native-worker-prepare', 'root-native-worker-bind',
  'root-native-worker-release', 'root-native-worker-values', 'root-native-worker-state',
  'root-native-worker-settle',
  'root-native-worker-peek', 'root-native-worker-join', 'root-native-worker-cancel',
  'root-native-pm-exceptions', 'root-native-thread-id',
  'root-native-process-test-prepare', 'root-native-process-test-execute',
  'root-native-process-test-cleanup',
]);
// The pinned worker controller returns arrays for these two commands. Bind
// echoes the seven input rows; cancel returns one result for each bound thread.
// All other endpoint commands return objects. Keep the reply shape tied to the
// request so a malformed reply still latches the shared no-replay guard.
function validateReply(command, value) {
  check(value !== null && typeof value === 'object', 'Malformed Seedsow retained reply value');
  if (!Array.isArray(value)) {
    check(!value.error, 'Seedsow retained command failed: ' + String(value.error));
    check(value.stopping !== true, 'Seedsow retained runner is stopping');
    check(command.command !== 'root-native-worker-bind' &&
      command.command !== 'root-native-worker-cancel',
    'Seedsow native worker reply is not an array');
    return value;
  }
  check(value.length === 7 && value.every(row => row && typeof row === 'object' &&
    !Array.isArray(row)), 'Malformed Seedsow native worker array');
  if (command.command === 'root-native-worker-bind') {
    check(Array.isArray(command.workers) && command.workers.length === 7 &&
      JSON.stringify(value) === JSON.stringify(command.workers),
    'Seedsow native worker binding differs from the owned request');
  } else if (command.command === 'root-native-worker-cancel') {
    check(value.every(row => row.result === 0 &&
      ((Number.isSafeInteger(row.handle) && row.handle > 0) ||
        (Number.isSafeInteger(row.id) && row.id > 0 && row.handle === null &&
          row.skipped === true))),
    'Seedsow native worker cancellation reply differs');
  } else check(false, 'Unexpected Seedsow retained array reply');
  return value;
}
function canonical(file, kind) {
  check(typeof file === 'string' && path.isAbsolute(file) && path.normalize(file) === file,
    'Invalid Seedsow ' + kind + ' path');
  const resolved = fs.realpathSync(file);
  // macOS reports os.tmpdir() below /var/folders, while /var is its fixed
  // system alias for /private/var. Admit only that exact alias; a symlink in
  // the private temp directory still changes the resolved suffix and fails.
  const macTempAlias = process.platform === 'darwin' && file.startsWith('/var/folders/') &&
    fs.lstatSync('/var').isSymbolicLink() && fs.readlinkSync('/var') === 'private/var' &&
    resolved === '/private' + file;
  check(resolved === file || macTempAlias, 'Symlink in Seedsow ' + kind + ' path');
  const state = fs.lstatSync(file);
  check(kind === 'endpoint' ? state.isSocket() : state.isFile(), 'Seedsow ' + kind + ' type differs');
  return state;
}
function identity(state) { return `${state.dev}:${state.ino}`; }
function create({socketPath, guard}) {
  check(guard && typeof guard.run === 'function' && typeof guard.fail === 'function' &&
    typeof guard.subscribe === 'function', 'Seedsow endpoint requires the shared transport guard');
  const original = canonical(socketPath, 'endpoint'), socketIdentity = identity(original);
  let active = null;
  let retireRequested = false;
  guard.subscribe(error => {
    if (!active) return;
    const current = active;
    active = null;
    clearTimeout(current.timer);
    current.socket.pause();
    current.reject(error);
  });
  function send(command, timeoutMs = 60000, retirement = false) {
    if (guard.error) return Promise.reject(guard.error);
    let bytes;
    try {
      check(!retireRequested, 'Seedsow retained endpoint retirement already requested');
      check(command && typeof command === 'object' && !Array.isArray(command) &&
        (ALLOWED.has(command.command) || retirement && command.command === 'retire' &&
          !retireRequested), 'Unsupported Seedsow retained command');
      if (retirement) retireRequested = true;
      check(Number.isInteger(timeoutMs) && timeoutMs > 0 && timeoutMs <= 20 * 60 * 1000,
        'Invalid Seedsow endpoint deadline');
      bytes = Buffer.from(JSON.stringify(command, (_, value) =>
        typeof value === 'bigint' ? value.toString() : value) + '\n');
      check(bytes.length <= MAX_REQUEST, 'Seedsow endpoint request exceeds its bound');
    } catch (error) { return Promise.reject(guard.fail(error)); }
    return guard.run(() => {
      check(identity(canonical(socketPath, 'endpoint')) === socketIdentity,
        'Seedsow retained endpoint instance changed');
      return new Promise((resolve, reject) => {
        const socket = net.createConnection(socketPath);
        let reply = Buffer.alloc(0), ended = false;
        const finish = (error, value) => {
          if (!active || active.socket !== socket) return;
          clearTimeout(active.timer);
          if (error) socket.pause();
          active = null;
          if (error) reject(guard.fail(error));
          else resolve(value);
        };
        active = {socket, reject, timer: setTimeout(() =>
          finish(Error('Seedsow retained endpoint deadline expired; result uncertain and not replayed')),
        timeoutMs)};
        socket.on('connect', () => {
          try { socket.write(bytes, error => { if (error) finish(error); }); }
          catch (error) { finish(error); }
        });
        socket.on('data', part => {
          if (guard.error || !active || active.socket !== socket) return;
          if (!Buffer.isBuffer(part) || reply.length + part.length > MAX_REPLY)
            return finish(Error('Seedsow retained reply exceeded its bound'));
          reply = Buffer.concat([reply, part]);
        });
        socket.on('end', () => {
          ended = true;
          try {
            check(reply.length > 1 && reply.at(-1) === 10 && reply.indexOf(10) === reply.length - 1,
              'Malformed Seedsow retained reply frame');
            const value = JSON.parse(reply.subarray(0, -1).toString('utf8'));
            finish(null, validateReply(command, value));
          } catch (error) { finish(error); }
        });
        socket.on('error', error => finish(error));
        socket.on('close', () => { if (!ended) finish(Error('Seedsow retained socket closed before reply')); });
      });
    }, 'endpoint');
  }
  function request(command, timeoutMs) { return send(command, timeoutMs); }
  async function retireOnce(timeoutMs = 120000) {
    try {
      const result = await send({command: 'retire'}, timeoutMs, true);
      check(result.retired === true || result.retiring === true,
        'Seedsow retained endpoint did not acknowledge retirement');
      return result;
    } catch (error) { throw guard.fail(error); }
  }
  async function read(address, length, runDirectory, command = 'read') {
    check(Number.isSafeInteger(address) && address > 0 && Number.isInteger(length) &&
      length > 0 && length <= 0x600000, 'Invalid Seedsow retained read');
    check(command === 'read' || command === 'retained-read', 'Invalid Seedsow read phase');
    check(typeof runDirectory === 'string' && path.isAbsolute(runDirectory) &&
      fs.realpathSync(runDirectory) === runDirectory, 'Invalid Seedsow retained run directory');
    const result = await request({command, address, length});
    try {
      const file = result.outputFile;
      check(typeof file === 'string' && file.startsWith(runDirectory + path.sep),
        'Seedsow retained output escaped its run');
      const stat = canonical(file, 'output');
      check(stat.size === length && result.address === address && result.length === length &&
        /^[a-f0-9]{64}$/.test(result.sha256), 'Seedsow retained output metadata differs');
      const handle = fs.openSync(file, fs.constants.O_RDONLY | fs.constants.O_NOFOLLOW);
      let bytes;
      try { bytes = fs.readFileSync(handle); } finally { fs.closeSync(handle); }
      check(bytes.length === length && crypto.createHash('sha256').update(bytes).digest('hex') === result.sha256,
        'Seedsow retained output bytes differ');
      return {result, bytes};
    } catch (error) { throw guard.fail(error); }
  }
  return Object.freeze({request, read, retireOnce, guard, socketPath, socketIdentity});
}
module.exports = {create, ALLOWED, MAX_REQUEST, MAX_REPLY, validateReply};
