// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Drop-in for the pinned engine's shared/rpc.cjs. Its upstream implementation
// rereads mutable connection.json and sends via a bridge that may reconnect,
// restart /eval or fall back after a lost reply. This adapter does none of those.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function create({rpc, connectionFile, connectionSha256, run}) {
  check(rpc && typeof rpc.status === 'function' && rpc.guard && rpc.binding,
    'Seedsow source adapter requires the owned typed RPC');
  check(typeof connectionFile === 'string' && path.isAbsolute(connectionFile) &&
    fs.realpathSync(connectionFile) === connectionFile && fs.lstatSync(connectionFile).isFile(),
  'Seedsow connection snapshot is not a canonical regular file');
  check(typeof run === 'string' && path.isAbsolute(run) && fs.realpathSync(run) === run &&
    fs.statSync(run).isDirectory(), 'Seedsow adapter run is not canonical');
  check(/^[a-f0-9]{64}$/.test(connectionSha256), 'Missing Seedsow connection digest');
  const original = fs.statSync(connectionFile), identity = `${original.dev}:${original.ino}`;
  function sameConnection() {
    check(fs.realpathSync(connectionFile) === connectionFile, 'Seedsow connection path changed');
    const now = fs.statSync(connectionFile);
    check(`${now.dev}:${now.ino}` === identity && sha(fs.readFileSync(connectionFile)) === connectionSha256,
      'Seedsow connection changed after admission');
  }
  sameConnection();
  const guard = rpc.guard, a = Object.freeze({...rpc.binding});
  async function checked(action) {
    if (guard.error) throw guard.error;
    try {
      sameConnection();
      const value = await action();
      sameConnection();
      return value;
    } catch (error) { throw guard.fail(error); }
  }
  const call = (offset, args) => checked(() => rpc.call(offset, args));
  const callAddress = (address, args) => checked(() => rpc.callAddress(address, args));
  const pair = calls => checked(async () => {
    check(Array.isArray(calls) && calls.length >= 1 && calls.length <= 2,
      'Invalid Seedsow native pair');
    const validated = calls.map(item => {
      check(item && typeof item === 'object' && !Array.isArray(item) &&
        Array.isArray(item.args) && item.args.length <= 4,
      'Invalid Seedsow paired call');
      const word = value => {
        check(typeof value === 'bigint' || typeof value === 'number' && Number.isSafeInteger(value) ||
          typeof value === 'string' && /^(?:0x[0-9a-f]+|[0-9]+)$/i.test(value),
        'Invalid Seedsow paired scalar');
        const number = BigInt(value);
        check(number >= 0n && number <= 0xffffffffffffffffn,
          'Seedsow paired scalar exceeds u64');
        return number;
      };
      return {offset: word(item.offset), args: item.args.map(word)};
    });
    const results = []; let output = Buffer.alloc(0);
    for (const item of validated) {
      const reply = await rpc.call(item.offset, item.args);
      results.push(reply.result); output = reply.output;
    }
    return {results, output};
  });
  const read = (address, length, timeoutMs) => checked(() => rpc.read(address, length, timeoutMs));
  const write = (address, bytes) => checked(() => rpc.write(address, bytes));
  const query = address => checked(() => rpc.query(address));
  const startWorkers = workers => checked(() => rpc.startWorkers(workers));
  const joinWorkers = workers => checked(() => rpc.joinWorkers(workers));
  const verifyFunction = (address, bytes) => checked(() => rpc.verifyFunction(address, bytes));
  const say = message => checked(() => rpc.say(message));
  const status = () => checked(async () => ({...await rpc.status(), ready: true}));
  async function request(body) {
    check(body && typeof body === 'object' && !Array.isArray(body), 'Invalid Seedsow bridge request');
    switch (body.command) {
      case 'status': return status();
      case 'call': {
        const reply = await call(body.offset, body.args);
        return {result: reply.result.toString(), output: reply.output.toString('base64')};
      }
      case 'call-address': {
        const reply = await callAddress(body.address, body.args);
        return {result: reply.result.toString(), output: reply.output.toString('base64')};
      }
      case 'pair': {
        check(Array.isArray(body.calls) && body.calls.length > 0 && body.calls.length <= 2,
          'Invalid Seedsow native pair');
        // Validate the entire pair before sending its first native call. A
        // malformed second item must not leave an unlatched partial batch.
        const word = value => {
          check(typeof value === 'bigint' || typeof value === 'number' && Number.isSafeInteger(value) ||
            typeof value === 'string' && /^(?:0x[0-9a-f]+|[0-9]+)$/i.test(value),
          'Invalid Seedsow paired scalar');
          const n = BigInt(value);
          check(n >= 0n && n <= 0xffffffffffffffffn, 'Seedsow paired scalar exceeds u64');
        };
        for (const item of body.calls) {
          check(item && typeof item === 'object' && !Array.isArray(item) &&
            Array.isArray(item.args) && item.args.length <= 4, 'Invalid Seedsow paired call');
          word(item.offset);
          for (const arg of item.args) word(arg);
        }
        const results = []; let output = Buffer.alloc(0);
        for (const item of body.calls) {
          const reply = await call(item.offset, item.args);
          results.push(reply.result.toString()); output = reply.output;
        }
        return {results, output: output.toString('base64')};
      }
      case 'read': return {data: (await read(body.address, body.length, body.timeoutMs)).toString('base64')};
      case 'write': {
        check(typeof body.data === 'string' && /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(body.data),
          'Invalid Seedsow write encoding');
        await write(body.address, Buffer.from(body.data, 'base64'));
        return {written: true};
      }
      case 'query': return query(body.address);
      case 'verify-function': return verifyFunction(body.address, body.hex);
      case 'workers-start': return {workers: await startWorkers(body.workers)};
      case 'workers-join': return {workers: await joinWorkers(body.workers)};
      case 'say': return say(body.text);
      default: throw guard.fail(Error('Unsupported Seedsow consume-only bridge command'));
    }
  }
  return Object.freeze({a, request, call, callAddress, pair, read, write, query, startWorkers,
    joinWorkers, verifyFunction, say, status, guard,
    get output() { return rpc.output; },
    get directory() { return path.join(run, 'switch2'); }});
}
module.exports = {create};
