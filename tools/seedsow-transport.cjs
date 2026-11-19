// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Consume an already-connected, exclusively owned Seedsow binary RPC socket.
// No connection creation, /eval, listener startup, retry, keepalive or cleanup.
// Wire contract reviewed at Malev0/seedsow-fs-kaihen 7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17:
// console/n2h_rpc.lua and tools/console-rpc.cjs. This does not import upstream.
// This low-level component does not establish source, boot or held-FS proof;
// the provider's live-admission gate remains closed until those are implemented.

const MAGIC = 0x5248324e, HEADER = 64, REPLY_HEADER = 24;
const LIMIT = 16 * 1024 * 1024, MAX_QUEUED = 16;
const COMMANDS = Object.freeze({status: 1, call: 2, callAddress: 3, read: 4,
  write: 5, query: 6, verify: 7, workersStart: 8, workersJoin: 9, ping: 10,
  stop: 11, say: 13});
const usedSockets = new WeakSet(), guards = new WeakSet();
const check = (ok, message) => { if (!ok) throw Error(message); };

// One non-resettable failure latch and queue for the socket and, eventually,
// the retained endpoint. A failure rejects pending work before another send.
function createGuard() {
  let error = null;
  // The retained endpoint runs in this same owner process and must itself
  // issue console RPC while its caller awaits a reply. Serialize each wire
  // independently under one non-resettable failure latch.
  const lanes = {console: {queue: Promise.resolve(), queued: 0},
    endpoint: {queue: Promise.resolve(), queued: 0}};
  const listeners = new Set();
  const guard = {
    get error() { return error; },
    fail(reason) {
      if (!error) {
        error = reason instanceof Error ? reason : Error(String(reason));
        error.nativeRequestUncertain = true;
        // A reporting callback must not prevent another owned transport from
        // receiving the stop signal. Keep the original uncertainty as cause.
        for (const listener of listeners) { try { listener(error); } catch {} }
      }
      return error;
    },
    run(operation, lane = 'console') {
      if (error) return Promise.reject(error);
      if (!Object.hasOwn(lanes, lane) || typeof operation !== 'function')
        return Promise.reject(guard.fail(Error('Invalid Seedsow transport operation lane')));
      const current = lanes[lane];
      if (current.queued >= MAX_QUEUED)
        return Promise.reject(guard.fail(Error('Seedsow operation queue exceeds its bound')));
      ++current.queued;
      const task = current.queue.then(async () => {
        if (error) throw error;
        try {
          const value = await operation();
          if (error) throw error;
          return value;
        } catch (reason) { throw guard.fail(reason); }
      });
      current.queue = task.then(() => { --current.queued; }, () => { --current.queued; });
      return task;
    },
    subscribe(listener) {
      listeners.add(listener);
      if (error) listener(error);
    },
  };
  guards.add(guard);
  return Object.freeze(guard);
}

function word(value) {
  check(typeof value === 'bigint' || typeof value === 'number' && Number.isSafeInteger(value) ||
    typeof value === 'string' && /^(?:0x[0-9a-f]+|[0-9]+)$/i.test(value), 'Invalid Seedsow scalar');
  const number = BigInt(value);
  check(number >= 0n && number <= 0xffffffffffffffffn, 'Seedsow scalar exceeds u64');
  return number;
}

function create(socket, guard = createGuard()) {
  check(guards.has(guard), 'Seedsow requires a shared non-resettable guard');
  check(socket && typeof socket.on === 'function' && typeof socket.write === 'function' &&
    typeof socket.pause === 'function' && typeof socket.listenerCount === 'function' &&
    socket.readable === true && socket.writable === true && !socket.destroyed && !socket.connecting &&
    socket.readableLength === 0 && socket.listenerCount('data') === 0 && !usedSockets.has(socket),
  'Seedsow requires an already-connected exclusively owned empty socket');
  check(!guard.error, 'Seedsow guard is already uncertain');
  usedSockets.add(socket);
  let pending = null;
  let stopQueued = false, stopped = false;
  guard.subscribe(error => {
    // Preserve ownership for inspection. Never send shutdown, end, destroy,
    // detach or retirement commands in response to an uncertain operation.
    if (pending) {
      const current = pending;
      pending = null;
      clearTimeout(current.timer);
      current.reject(error);
    }
    socket.pause();
  });
  for (const event of ['end', 'close'])
    socket.on(event, () => {
      if (!stopped) guard.fail(Error('Seedsow RPC socket ' + event + '; no reconnect or replay'));
    });
  socket.on('error', error => guard.fail(error));
  socket.on('data', chunk => {
    if (guard.error) return;
    try {
      check(Buffer.isBuffer(chunk), 'Seedsow RPC socket must deliver bytes');
      if (!chunk.length) return;
      check(pending, 'Unsolicited Seedsow RPC reply');
      const current = pending;
      let at = 0;
      if (current.headerUsed < REPLY_HEADER) {
        const size = Math.min(REPLY_HEADER - current.headerUsed, chunk.length);
        chunk.copy(current.header, current.headerUsed, 0, size);
        current.headerUsed += size;
        at += size;
        if (current.headerUsed < REPLY_HEADER) return;
        check(current.header.readUInt32LE(0) === MAGIC, 'Invalid Seedsow reply magic');
        const length = current.header.readBigUInt64LE(8);
        check(length <= BigInt(current.maxReplyBytes), 'Seedsow RPC reply exceeds its bound');
        if (current.replyBytes !== undefined)
          check(length === BigInt(current.replyBytes), 'Seedsow RPC reply length differs');
        current.payload = Buffer.alloc(Number(length));
      }
      const remaining = current.payload.length - current.used;
      check(chunk.length - at <= remaining, 'Trailing or duplicate Seedsow RPC reply');
      chunk.copy(current.payload, current.used, at);
      current.used += chunk.length - at;
      if (current.used !== current.payload.length) return;
      check(current.header.readUInt32LE(4) === 0, 'Seedsow RPC reported an error: ' +
        current.payload.subarray(0, 200).toString('utf8'));
      const reply = {aux: current.header.readBigInt64LE(16), payload: current.payload};
      const value = current.decode(reply);
      check(!value || typeof value.then !== 'function', 'Seedsow reply decoder must be synchronous');
      if (guard.error) throw guard.error;
      if (current.command === 'stop') stopped = true;
      pending = null;
      clearTimeout(current.timer);
      current.resolve(value);
    } catch (error) { guard.fail(error); }
  });

  function request(command, options = {}, decode = reply => reply) {
    // Snapshot bytes/scalars before queueing so a queued command cannot drift.
    // No script-eval or reconnect opcode is exposed here. The one stop opcode
    // is issued only after a qualified pre-FS failure or completed FS/HEN
    // retirement, and cannot be replayed.
    if (guard.error) return Promise.reject(guard.error);
    let body, timeoutMs, maxReplyBytes, replyBytes;
    try {
      check(!stopQueued && !stopped, 'Seedsow RPC listener was already stopped');
      check(Object.hasOwn(COMMANDS, command), 'Unsupported Seedsow consume-only command');
      if (command === 'stop') stopQueued = true;
      check(options && typeof options === 'object' && !Array.isArray(options) &&
        Object.keys(options).every(key => ['a0', 'a1', 'a2', 'a3', 'a4', 'payload',
          'timeoutMs', 'maxReplyBytes', 'replyBytes'].includes(key)), 'Unsupported Seedsow request option');
      const {payload = Buffer.alloc(0)} = options;
      ({timeoutMs = 120000, maxReplyBytes = LIMIT, replyBytes} = options);
      check(Buffer.isBuffer(payload) && payload.length <= LIMIT, 'Seedsow request payload exceeds its bound');
      check(Number.isInteger(timeoutMs) && timeoutMs > 0 && timeoutMs <= 300000, 'Invalid Seedsow deadline');
      check(Number.isInteger(maxReplyBytes) && maxReplyBytes >= 0 && maxReplyBytes <= LIMIT,
        'Invalid Seedsow reply bound');
      check(replyBytes === undefined || Number.isInteger(replyBytes) && replyBytes >= 0 &&
        replyBytes <= maxReplyBytes, 'Invalid Seedsow exact reply length');
      check(typeof decode === 'function', 'Missing Seedsow reply decoder');
      body = Buffer.alloc(HEADER + payload.length);
      body.writeUInt32LE(MAGIC, 0);
      body.writeUInt32LE(COMMANDS[command], 4);
      for (let i = 0; i < 5; ++i) body.writeBigUInt64LE(word(options['a' + i] ?? 0), 8 + i * 8);
      body.writeUInt32LE(payload.length, 48);
      payload.copy(body, HEADER);
    } catch (error) { return Promise.reject(guard.fail(error)); }
    return guard.run(() => {
      check(!pending && !socket.destroyed && socket.readable && socket.writable,
        'Seedsow socket is no longer owned and ready');
      return new Promise((resolve, reject) => {
        pending = {resolve, reject, decode, command, maxReplyBytes, replyBytes, header: Buffer.alloc(REPLY_HEADER),
          headerUsed: 0, payload: null, used: 0, timer: setTimeout(() => guard.fail(
            Error('Seedsow RPC deadline expired; result uncertain and request was not replayed')), timeoutMs)};
        try { socket.write(body, error => { if (error) guard.fail(error); }); }
        catch (error) { guard.fail(error); }
      });
    });
  }
  return Object.freeze({request, guard});
}

module.exports = {create, createGuard, COMMANDS, LIMIT, MAX_QUEUED};
