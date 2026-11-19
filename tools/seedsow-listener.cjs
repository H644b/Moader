// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Starts the pinned save's binary listener exactly once through its launcher.
// The live TCP socket is then exclusively owned by seedsow-transport. This
// never imports the upstream bridge, which can reconnect and replay commands.
const fs = require('node:fs');
const path = require('node:path');
const net = require('node:net');
const http = require('node:http');
const crypto = require('node:crypto');
const transport = require('./seedsow-transport.cjs');
const typed = require('./seedsow-rpc.cjs');
const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const SERVE_SECONDS = 7200, IDLE_SECONDS = 7200;
const ROOT = path.resolve(__dirname, '..');

function pinnedShim() {
  const profile = require('./seedsow-provider.cjs').resolveProfile(ROOT);
  const file = path.join(profile.sourceDirectory, 'console/n2h_rpc.lua');
  check(fs.realpathSync(file) === file && fs.lstatSync(file).isFile(),
    'Pinned Seedsow console shim is unavailable');
  const bytes = fs.readFileSync(file);
  check(bytes.length > 0 && bytes.length < 0x10000 &&
    sha(bytes) === profile.files['console/n2h_rpc.lua'] &&
    !bytes.includes(Buffer.from(']=]')),
  'Pinned Seedsow console shim changed or cannot be embedded');
  return bytes;
}

function connection(host, port, timeoutMs = 1500) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({host, port});
    const timer = setTimeout(() => { socket.destroy(); reject(Error('Seedsow TCP connect deadline')); }, timeoutMs);
    socket.once('connect', () => {
      clearTimeout(timer); socket.removeAllListeners('error'); socket.setNoDelay(true); resolve(socket);
    });
    socket.once('error', error => { clearTimeout(timer); reject(error); });
  });
}
function startServe(host, httpPort, rpcPort, guard, run, shim, stopState) {
  // The launcher accepts the reviewed shim as one eval body. Run those exact
  // pinned bytes in memory; the installed save needs no RPC file or update.
  const source = Buffer.concat([Buffer.from('assert(_G.__rpc==nil,"RPC already active")\n' +
    'assert(loadstring([=['), shim, Buffer.from(']=]))()\n' +
    `print(__rpc.serve(${rpcPort}, ${SERVE_SECONDS}, ${IDLE_SECONDS}, "blocking"))\n`)]);
  const request = http.request({host, port: httpPort, path: '/eval', method: 'POST',
    headers: {'content-type': 'text/plain', 'content-length': source.length}}, response => {
    let size = 0;
    const output = [];
    response.on('data', bytes => {
      size += bytes.length;
      if (size > 4096) guard.fail(Error('Seedsow serve reply exceeded its bound'));
      else output.push(bytes);
    });
    response.on('error', error => guard.fail(error));
    response.on('end', () => {
      const body = Buffer.concat(output).toString('utf8');
      try {
        fs.writeFileSync(path.join(run, 'moader-listener-response.json'),
          JSON.stringify({httpStatus: response.statusCode, body, ended: new Date().toISOString()},
            null, 2) + '\n', {flag: 'wx', mode: 0o600});
      } catch (error) { guard.fail(error); }
      stopState.response = {statusCode: response.statusCode, body};
      if (!stopState.requested || stopState.confirmed &&
          !(response.statusCode === 200 && body.includes('SERVE DONE')))
        guard.fail(Error('Seedsow serve loop ended: HTTP ' + response.statusCode +
          ', ' + body.trim().slice(-300) + '; current native result is uncertain'));
      else if (stopState.confirmed) stopState.ended = true;
    });
  });
  request.on('error', error => guard.fail(error));
  request.end(source);
  return request; // Keep the only /eval launch request owned until explicit recovery.
}
function decodeStatus(reply) {
  check(reply.aux === 0n && reply.payload.length > 0 && reply.payload.length <= 256,
    'Seedsow listener status frame differs');
  const parts = reply.payload.toString('ascii').split('\0');
  check(parts.length === 5 && parts[0] === 'S' && parts[4] === 'seedsow' &&
    parts.slice(1, 4).every(value => /^(?:0|[1-9][0-9]*)$/.test(value)),
  'Seedsow listener status identity differs');
  const [sdk, main, scratch] = parts.slice(1, 4).map(Number);
  check([sdk, main, scratch].every(value => Number.isSafeInteger(value) && value > 0 && value % 4096 === 0),
    'Seedsow listener mapping is invalid');
  return {platform: 2, sdk, main, scratch, session: 'seedsow'};
}
async function open({host, httpPort = 8080, rpcPort = 39380, run, preflightFile,
  guard = transport.createGuard()}) {
  check(net.isIP(host) === 4 && host !== '0.0.0.0' &&
    Number.isInteger(httpPort) && httpPort > 0 && httpPort <= 65535 &&
    Number.isInteger(rpcPort) && rpcPort > 0 && rpcPort <= 65535,
  'Invalid Seedsow console address or listener ports');
  check(typeof run === 'string' && path.isAbsolute(run) && fs.realpathSync(run) === run &&
    fs.statSync(run).isDirectory(), 'Seedsow listener run is not canonical');
  check(typeof preflightFile === 'string' && path.isAbsolute(preflightFile) &&
    fs.realpathSync(preflightFile) === preflightFile && fs.lstatSync(preflightFile).isFile(),
  'Seedsow preflight report is not canonical');
  const captured = fs.readFileSync(preflightFile), preflight = JSON.parse(captured);
  check(preflight.provider === 'seedsow-malev0' && preflight.complete === true &&
    preflight.console === host && preflight.port === httpPort &&
    preflight.allRequiredPinnedSignaturesMatch === true &&
    preflight.fsAcquisitionAttempted === false && preflight.codeOrKernelPatchAttempted === false &&
    Number.isSafeInteger(preflight.moduleBases?.sdk) &&
    Number.isSafeInteger(preflight.moduleBases?.main),
  'Current Seedsow read-only preflight does not match the requested Demo');
  const shim = pinnedShim();
  // Refuse an inherited listener. There is no safe way to claim a socket that
  // another bridge or boot may own, even if its literal session says seedsow.
  try {
    const inherited = await connection(host, rpcPort, 1200);
    inherited.end();
    throw Error('Seedsow binary listener already exists; ownership is unknown');
  } catch (error) {
    if (error.message.includes('ownership is unknown')) throw error;
    check(error.code === 'ECONNREFUSED',
      'Seedsow binary listener state is uncertain: ' + error.message);
  }
  const stopState = {requested: false, confirmed: false, ended: false, response: null};
  const serveRequest = startServe(host, httpPort, rpcPort, guard, run, shim, stopState);
  let socket = null;
  for (let attempt = 0; attempt < 60 && !guard.error; ++attempt) {
    await delay(250);
    try { socket = await connection(host, rpcPort, 1200); break; }
    catch (error) {
      if (error.code !== 'ECONNREFUSED') throw guard.fail(error);
    }
  }
  if (!socket) throw guard.fail(Error('Seedsow listener did not accept a single owned connection'));
  const wire = transport.create(socket, guard);
  const observed = await wire.request('status', {maxReplyBytes: 256}, decodeStatus);
  if (observed.sdk !== preflight.moduleBases.sdk ||
      observed.main !== preflight.moduleBases.main)
    throw guard.fail(Error('Seedsow listener mapping differs from the current read-only capture'));
  const rpc = typed.create(wire, observed);
  await rpc.status();
  const nonce = crypto.randomInt(1, 0x7fffffff);
  if (!await rpc.ping(nonce))
    throw guard.fail(Error('Seedsow current listener did not echo its fresh challenge'));
  const snapshot = {schema: 1, provider: 'seedsow-malev0', console: host,
    httpPort, rpcPort, addresses: observed, challenge: nonce,
    preflightSha256: sha(captured), shimSha256: sha(shim), acquired: false};
  const connectionFile = path.join(run, 'connection.json');
  const bytes = Buffer.from(JSON.stringify(snapshot, null, 2) + '\n');
  fs.writeFileSync(connectionFile, bytes, {flag: 'wx', mode: 0o400});
  async function stopAfterProvenFailure() {
    check(!stopState.requested && !guard.error, 'Seedsow listener is not available for a unique stop');
    stopState.requested = true;
    await wire.request('stop', {replyBytes: 0, timeoutMs: 8000}, reply => {
      check(reply.aux === 0n && reply.payload.length === 0,
        'Seedsow listener stop reply differs');
      stopState.confirmed = true;
      if (stopState.response) {
        check(stopState.response.statusCode === 200 &&
          stopState.response.body.includes('SERVE DONE'),
        'Seedsow serve completion differs from the acknowledged stop');
        stopState.ended = true;
      }
      return true;
    });
    const deadline = Date.now() + 8000;
    while (!stopState.ended && !guard.error && Date.now() < deadline) await delay(50);
    if (!stopState.ended) throw guard.fail(Error('Seedsow listener stop was acknowledged but serve completion is uncertain'));
    return {stopped: true, response: path.join(run, 'moader-listener-response.json')};
  }
  return Object.freeze({rpc, guard, socket, serveRequest, connectionFile,
    connectionSha256: sha(bytes), snapshot, stopAfterProvenFailure});
}
module.exports = {open, connection, decodeStatus, SERVE_SECONDS, IDLE_SECONDS};
