'use strict';

// No transport imports. The caller supplies an independently qualified native
// reader and a shared stop latch; read() must not use retained FS or a PM worker.
const {performance} = require('node:perf_hooks');
const activeModules = new WeakSet(), activeObservers = new WeakSet();
const OBSERVATIONS = 30, POLL_MS = 25, DEADLINE_MS = OBSERVATIONS * POLL_MS;
const check = (ok, message) => { if (!ok) throw Error(message); };
const u32 = n => { const bytes = Buffer.alloc(4); bytes.writeUInt32LE(n); return bytes; };
const address = n => Number.isSafeInteger(n) && n > 0;
const handle = n => Number.isInteger(n) && n > 0 && n <= 0xffffffff;
function pointer(bytes, offset) {
  const value = bytes.readBigUInt64LE(offset);
  check(value <= BigInt(Number.MAX_SAFE_INTEGER), 'Loader pointer exceeds exact host range');
  return Number(value);
}

async function withGateRestoration(gateModule, observer, operation) {
  const descriptor = gateModule && Object.getOwnPropertyDescriptor(gateModule, 'Gate');
  const BaseGate = descriptor?.value;
  check(descriptor?.writable && typeof BaseGate === 'function' && BaseGate.prototype &&
    ['restore', 'inspect', 'selections'].every(name => typeof BaseGate.prototype[name] === 'function'),
  'Unsupported Loader Gate module');
  check(observer && typeof observer.read === 'function' && typeof observer.block === 'function' &&
    typeof observer.uncertain === 'boolean' &&
    (observer.now === undefined || typeof observer.now === 'function') &&
    (observer.delay === undefined || typeof observer.delay === 'function'),
  'Loader restoration requires an independent observer and shared stop latch');
  check(typeof operation === 'function', 'Loader restoration operation is required');
  check(!activeModules.has(gateModule) && !activeObservers.has(observer), 'Loader restoration wrapper is already active');
  check(!observer.uncertain, 'Loader restoration observer is already uncertain');

  const now = observer.now ? observer.now.bind(observer) : () => performance.now();
  const delay = observer.delay ? observer.delay.bind(observer) : ms => new Promise(resolve => setTimeout(resolve, ms));
  const states = new WeakMap();
  let firstFailure;
  function fail(gate, state, value) {
    const error = value instanceof Error ? value : Error(String(value));
    error.loaderRestorationUnresolved = true;
    error.inspectBeforeRetry = true;
    state.error ||= error;
    firstFailure ||= state.error;
    gate.restoring = true;
    gate.restored = false;
    Object.assign(gate.restoration, {phase: 'unresolved', unresolved: true, restoring: true,
      error: state.error.message});
    // block() is synchronous and idempotent. It must also stop upstream finally
    // blocks and observer cleanup; this helper performs no native cleanup here.
    try { observer.block(state.error); }
    catch (blockError) { gate.restoration.blockError = String(blockError.message || blockError); }
    return state.error;
  }

  class ReceiptGate extends BaseGate {
    async restore(...args) {
      // Preserve the upstream non-event implementation without alteration.
      if (!this.event) return super.restore(...args);
      const prior = states.get(this);
      if (prior) {
        if (prior.complete) return;
        throw fail(this, prior, prior.error || Error('Loader restoration is already in progress; replay is forbidden'));
      }
      if (!this.parked) return;
      const state = {pending: false, complete: false, error: null};
      states.set(this, state);
      const record = this.restoration = {event: true, snapshotRetired: false, restoring: true,
        unresolved: true, phase: 'validate', observations: 0, deadlineMs: DEADLINE_MS};
      this.restoring = true;
      this.restored = false;
      const safe = () => {
        check(!state.error && !observer.uncertain, 'Loader restoration is blocked by an unresolved native operation');
      };
      const call = async (phase, fn) => {
        safe(); record.phase = phase; state.pending = true;
        try { const result = await fn(); state.pending = false; safe(); return result; }
        catch (error) { state.pending = false; throw error; }
      };
      const read = async (at, length, phase) => {
        const bytes = await call(phase, () => observer.read(at, length));
        check(Buffer.isBuffer(bytes) && bytes.length === length, 'Independent Loader observation has invalid length');
        return bytes;
      };
      const inspect = async parked => {
        const bytes = await read(this.row.address, 0x88, parked ? 'inspect-parked' : 'observe-receipt');
        check(pointer(bytes, 0) === this.vptr && pointer(bytes, 0x70) === this.row.domain &&
          pointer(bytes, 0x78) === this.row.metadata && bytes.readUInt32LE(0x48) === this.row.receive &&
          pointer(bytes, 0x60) === this.row.request && pointer(bytes, 0x68) === 0x100,
        'Loader session identity changed during restoration');
        check(bytes.readUInt32LE(0x30) === (parked ? this.park : this.row.wait),
          'Loader wait handle changed during restoration');
        check(bytes.readBigUInt64LE(0x38) === 1n, 'Loader session tag is not 1');
        return {parent: pointer(bytes, 0x28), state: bytes.readUInt32LE(0x40),
          receiveResult: bytes.readUInt32LE(0x44)};
      };
      const idle = view => check(view.state === 1 && this.waiters.includes(view.parent),
        'Loader session is not idle on a known waiter');
      const unselected = async () => {
        const bytes = await read(this.waiters[0] + 0x10, 6 * 0x110 + 8, 'inspect-selections');
        check(this.waiters.every((_, index) => pointer(bytes, index * 0x110) !== this.row.address),
          'Loader session still has a selected waiter');
      };
      let lastTime = -Infinity;
      const time = () => {
        const value = now();
        check(Number.isFinite(value) && value >= lastTime, 'Loader restoration clock is not monotonic');
        lastTime = value; return value;
      };
      const eventRequest = async (command, eventHandle, phase) => {
        const reply = await call(phase, () => this.memory.request({command, handle: eventHandle}));
        check(reply && Number.isInteger(reply.result), 'Malformed Loader event reply');
        return reply;
      };
      const clear = async () => {
        check(!record.clearAttempted, 'Loader event clear replay is forbidden');
        record.clearAttempted = true;
        const reply = await eventRequest('root-native-event-clear', this.event.read, 'clear-event');
        record.clear = {result: reply.result, ...(reply.at === undefined ? {} : {at: reply.at})};
        check(reply.result === 0, 'Loader restoration event did not clear');
      };
      try {
        check(this.row && ['address', 'domain', 'metadata', 'request'].every(key => address(this.row[key])) &&
          address(this.vptr) && handle(this.row.wait) && handle(this.row.receive) && handle(this.park) &&
          handle(this.event.read) && handle(this.event.write) && this.park === this.event.sourceHandle &&
          this.park !== this.row.wait && this.event.read !== this.event.write &&
          Array.isArray(this.waiters) && this.waiters.length === 7 &&
          this.waiters.every((at, index) => address(at) && at === this.waiters[0] + index * 0x110) &&
          typeof this.memory?.write === 'function' && typeof this.memory?.request === 'function',
        'Unsupported event-based Loader gate configuration');
        idle(await inspect(true));
        await unselected();
        const header = await read(this.row.request, 4, 'inspect-request-marker');
        if (this.outstanding) {
          check(typeof this.outstanding.original === 'string' && /^[0-9a-f]{8}$/i.test(this.outstanding.original) &&
            this.outstanding.original.toLowerCase() !== 'ffffffff', 'Outstanding Loader receipt has no valid original header');
          if (header.readUInt32LE() === 0xffffffff) {
            await call('rollback-request-marker', () => this.memory.write(this.row.request, u32(0xffffffff),
              Buffer.from(this.outstanding.original, 'hex')));
            record.requestMarkerRolledBack = true;
          }
        } else check(header.readUInt32LE() !== 0xffffffff, 'Unowned Loader request marker cannot be restored');

        const view = await inspect(true);
        idle(view); await unselected();
        const waiting = await eventRequest('root-native-wait-zero', this.event.read, 'verify-event-clear');
        check(waiting.result === 0xea01, 'Loader parking event must be unsignaled before arming receipt');
        const previous = view.receiveResult & 0xff;
        check(previous <= 3, 'Loader receive result is not a known pre-cookie result');
        await call('arm-receipt-cookie', () => this.memory.write(this.row.address + 0x44,
          Buffer.from([previous]), Buffer.from([0xff])));
        record.cookieArmed = true;
        await call('publish-original-wait', () => this.memory.write(this.row.address + 0x30,
          u32(this.park), u32(this.row.wait)));
        record.waitRestored = true;
        // parked remains a responsibility flag until the receipt AND clear.
        const started = time();
        record.startedMs = started;
        record.signalAttempted = true;
        const signal = await eventRequest('root-native-event-signal', this.event.write, 'signal-event');
        record.signal = {result: signal.result, ...(signal.at === undefined ? {} : {at: signal.at})};
        check(signal.result === 0, 'Loader restoration event did not signal');
        for (let index = 0; index < OBSERVATIONS; index++) {
          if (time() - started >= DEADLINE_MS) break;
          record.observations++;
          const current = await inspect(false);
          const elapsed = time() - started;
          record.lastObservation = {...current, receipt: current.receiveResult & 0xff, elapsedMs: elapsed};
          const received = (current.receiveResult & 0xff) <= 3;
          if (received) record.receiptObserved = {value: current.receiveResult & 0xff,
            elapsedMs: elapsed, withinDeadline: elapsed < DEADLINE_MS};
          // Epoch/parent movement is diagnostic only. A real receive must
          // overwrite our low-byte cookie while exact identity/wait still match.
          // A read completing after the deadline is intentionally unresolved,
          // even if it sees a receipt; retain that evidence in receiptObserved.
          if (elapsed < DEADLINE_MS && received) {
            record.receiptSeen = true;
            await clear();
            state.complete = true;
            this.parked = false; this.restoring = false; this.restored = true;
            delete this.outstanding;
            Object.assign(record, {phase: 'restored', snapshotRetired: true, restoring: false, unresolved: false});
            return;
          }
          const remaining = DEADLINE_MS - (time() - started);
          if (index + 1 < OBSERVATIONS && remaining > 0)
            await call('wait-for-receipt', () => delay(Math.min(POLL_MS, remaining)));
        }
        // A fully returned polling expiry is definite, so a single clear is
        // allowed. No cleanup follows a failed/pending observation or clear.
        safe();
        check(!state.pending, 'Loader observation is still pending');
        record.pollingExpired = true;
        await clear();
        const error = Error('Loader restoration receipt deadline expired; native state remains unresolved');
        error.code = 'LOADER_RESTORATION_EXPIRED';
        throw error;
      } catch (error) { throw fail(this, state, error); }
    }
  }

  activeModules.add(gateModule); activeObservers.add(observer);
  gateModule.Gate = ReceiptGate;
  try {
    const result = await operation();
    if (firstFailure) throw firstFailure;
    return result;
  } finally {
    // JS export restoration is safe even while all native calls are forbidden.
    gateModule.Gate = BaseGate;
    activeModules.delete(gateModule); activeObservers.delete(observer);
  }
}

module.exports = {withGateRestoration};
