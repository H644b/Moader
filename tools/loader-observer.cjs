'use strict';
// Read the parked Loader through the still-running diagnostic RO. This path
// uses process-memory IPC, never a retained FS read or the busy PM worker.
// Importing this module does not load a run or open a transport.
const FS_PROGRAM = 0x0400000000000000n;
const READ_PROCESS = 0x524f0011;
const check = (ok, message) => { if (!ok) throw Error(message); };

function readReply(reply, pid, length) {
  const bytes = reply?.data;
  // The pinned SDK client reads a fixed 256-byte CMIF frame. Its response()
  // exposes all 208 bytes after SFCO, including unused zero padding.
  check(reply?.result === 0 && Buffer.isBuffer(bytes) && bytes.length === 208 &&
    length > 0 && length <= 128,
    'Loader observer process read failed or was truncated');
  check(bytes.readBigUInt64LE(0) === BigInt(pid) && bytes.readBigUInt64LE(8) === FS_PROGRAM &&
    bytes.readBigUInt64LE(16) === 0n && bytes.readBigUInt64LE(32) === 0n &&
    bytes.readBigUInt64LE(40) === 0n && bytes.readBigUInt64LE(48) === BigInt(length),
  'Loader observer identity, mapping or alias cleanup differs');
  check(bytes.subarray(56 + length).every(byte => byte === 0),
    'Loader observer reply has unexpected data beyond its declared read');
  return Buffer.from(bytes.subarray(56, 56 + length));
}

async function openObserver({run, memory, execution, state, record, save}, deps = {}) {
  check(state && record && typeof save === 'function', 'Loader observer needs a shared safety latch and journal');
  let client, closed = false;
  const uncertain = () => !!state.error || !!client?.pending;
  const block = error => {
    error.nativeRequestUncertain = true;
    state.error ||= error;
    record.resourcesRetained = true;
    record.unresolved = true;
    record.error = String(error.stack || error);
    return state.error;
  };
  try {
    const open = deps.openClient || require('../runtime/modloader/lib/native/client.cjs').openClient;
    client = await open(run);
    check(!client.pending && client.record.session === memory.info.session &&
      client.record.report === memory.reportFile && String(client.pid) === String(execution.pid),
    'Loader observer is not the verified current diagnostic RO');
    record.roPid = String(client.pid);
    record.protocolEvidence = client.file;
    const handle = await client.fsProcess();
    const identity = await client.info(handle);
    check(identity.result === 0 && identity.programId === FS_PROGRAM.toString(16).padStart(16, '0') &&
      /^\d+$/.test(String(identity.pid)) && BigInt(identity.pid) > 1n,
    'Loader observer exported process is not native FS');
    const pid = BigInt(identity.pid);
    record.fsPid = String(pid);
    record.open = true;
    await save();
    const observer = {
      get uncertain() { return uncertain(); },
      block,
      async read(address, length) {
        if (uncertain()) throw state.error || block(Error('Loader observer has a pending native request'));
        check(!closed && Number.isSafeInteger(address) && address >= 2 ** 32 &&
          Number.isInteger(length) && length > 0 && length <= 0x1000 && address + length <= 2 ** 39,
        'Invalid Loader observer read');
        try {
          const parts = [];
          for (let offset = 0; offset < length;) {
            const at = address + offset, count = Math.min(128, length - offset, 4096 - at % 4096);
            const reply = await client.request(READ_PROCESS, handle, [pid, BigInt(at), BigInt(count), 0n]);
            parts.push(readReply(reply, pid, count));
            offset += count;
          }
          return Buffer.concat(parts, length);
        } catch (error) {
          block(error);
          await save();
          throw error;
        }
      },
      async qualify(row) {
        try {
          check(row && Number.isSafeInteger(row.address), 'Missing bound Loader session');
          const bytes = await observer.read(row.address, 0x88);
          const original = Buffer.from(row.raw || '', 'hex');
          check(original.length === 0x88 && bytes.readBigUInt64LE(0) === original.readBigUInt64LE(0) &&
            bytes.readBigUInt64LE(0x70) === BigInt(row.domain) &&
            bytes.readBigUInt64LE(0x78) === BigInt(row.metadata) &&
            bytes.readUInt32LE(0x30) === row.wait && bytes.readUInt32LE(0x48) === row.receive &&
            bytes.readBigUInt64LE(0x60) === BigInt(row.request) && bytes.readBigUInt64LE(0x68) === 0x100n,
          'Independent Loader view differs from its current binding');
          record.preflightReadVerified = true;
          await save();
        } catch (error) {
          block(error);
          await save();
          throw error;
        }
      },
      async close() {
        if (closed) return;
        if (uncertain()) {
          record.resourcesRetained = true;
          await save();
          return;
        }
        try {
          await client.close();
          check(!client.pending, 'Loader observer cleanup remains pending');
          closed = true;
          record.open = false;
          record.closed = true;
          record.resourcesRetained = false;
          await save();
        } catch (error) {
          block(error);
          await save();
          throw error;
        }
      },
    };
    return observer;
  } catch (error) {
    // An unexpected identity cannot authorize cleanup on a different server.
    // Open failures may also leave the SDK/worker request pending.
    block(error);
    await save();
    throw error;
  }
}

module.exports = {openObserver, readReply};
