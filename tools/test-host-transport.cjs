#!/usr/bin/env node
'use strict';
// Independent protocol checks with fake streams. No network or console calls.
const assert = require('node:assert/strict');
const {EventEmitter} = require('node:events');
let forbidden = 0;
const deny = () => { forbidden++; throw Error('Transport tests forbid real network IO'); };
require('node:http').request = deny;
require('node:net').createConnection = deny;
const {create, LIMIT} = require('./host-transport.cjs');
const connection = {port: 12345, token: '0'.repeat(64)};

function fakeHttp(handler) {
  const calls = [];
  return {calls, request(options, receive) {
    const request = new EventEmitter();
    request.setTimeout = (milliseconds, callback) => { request.timeout = callback; request.milliseconds = milliseconds; };
    request.destroy = () => { request.destroyed = true; };
    request.end = body => {
      const call = {options, body: JSON.parse(body), request}; calls.push(call);
      queueMicrotask(() => {
        try {
          handler({...call, reply(value, mode = {}) {
            const response = new EventEmitter();
            response.statusCode = mode.status || 200; response.complete = mode.complete !== false;
            receive(response);
            if (mode.aborted) { response.emit('aborted'); response.emit('close'); return; }
            const bytes = mode.raw || Buffer.from(JSON.stringify(value));
            response.emit('data', bytes);
            if (!mode.closeOnly) response.emit('end');
            response.emit('close');
          }});
        } catch (error) { request.emit('error', error); }
      });
    };
    return request;
  }};
}

function fakeNet(handler) {
  const calls = [];
  return {calls, createConnection(target) {
    const socket = new EventEmitter();
    const call = {target, socket}; calls.push(call);
    socket.setTimeout = (milliseconds, callback) => { socket.timeout = callback; socket.milliseconds = milliseconds; };
    socket.destroy = () => { socket.destroyed = true; socket.emit('close'); };
    socket.write = line => {
      assert.ok(line.endsWith('\n')); call.command = JSON.parse(line);
      handler({...call, reply(value, mode = {}) {
        if (mode.error) { socket.emit('error', mode.error); return; }
        socket.emit('data', mode.raw || Buffer.from(JSON.stringify(value)));
        if (!mode.closeOnly) socket.emit('end');
        socket.emit('close');
      }});
    };
    queueMicrotask(() => socket.emit('connect'));
    return socket;
  }};
}

let passed = 0;
async function test(name, action) { await action(); passed++; console.log('PASS: ' + name); }
async function stopped(transport, count) {
  const error = transport.state.error;
  assert.ok(error?.nativeRequestUncertain);
  await assert.rejects(() => transport.rpc.query(0), candidate => candidate === error);
  await assert.rejects(() => transport.rpc.callAddress6(0, []), candidate => candidate === error);
  await assert.rejects(() => transport.endpointRequest('/synthetic/memory.sock', {command: 'cleanup'}), candidate => candidate === error);
  assert.equal(count(), 1, 'A failure dispatched a retry or later cleanup');
}

(async () => {
  await test('loopback HTTP auth, six arguments and 64-bit values retain their wire form', async () => {
    const web = fakeHttp(call => {
      assert.equal(call.options.host, '127.0.0.1'); assert.equal(call.options.port, connection.port);
      assert.equal(call.options.headers.authorization, 'Bearer ' + connection.token);
      assert.equal(call.body.command, 'call-address-6'); assert.equal(call.body.address, '4294967312');
      assert.deepEqual(call.body.args, ['1', '2', '3', '4', '5', '6']);
      call.reply({result: '4294967300', output: Buffer.from('reply').toString('base64')});
    });
    const transport = create(connection, {error: null}, {http: web, net: fakeNet(deny)});
    const reply = await transport.rpc.callAddress6(4294967312n, [1n, 2n, 3n, 4n, 5n, 6n]);
    assert.equal(reply.result, 4294967300n); assert.equal(reply.output.toString(), 'reply');
    assert.equal(web.calls.length, 1); assert.equal(transport.state.error, null);
  });

  await test('bounded multi-chunk reads preserve length and address progression', async () => {
    const web = fakeHttp(call => call.reply({data: Buffer.alloc(call.body.length, web.calls.length).toString('base64')}));
    const transport = create(connection, {error: null}, {http: web, net: fakeNet(deny)});
    const bytes = await transport.rpc.read(4294967296, 0x80001);
    assert.equal(bytes.length, 0x80001); assert.equal(bytes[0], 1); assert.equal(bytes[0x80000], 2);
    assert.deepEqual(web.calls.map(call => [call.body.address, call.body.length]), [[4294967296, 0x80000], [4295491584, 1]]);
  });

  for (const [name, handler] of [
    ['timeout', call => call.request.timeout()],
    ['aborted response', call => call.reply({}, {aborted: true})],
    ['disconnect without end', call => call.reply({}, {closeOnly: true})],
    ['incomplete HTTP framing', call => call.reply({}, {complete: false})],
    ['malformed JSON', call => call.reply(null, {raw: Buffer.from('{')})],
    ['oversized response', call => call.reply(null, {raw: Buffer.alloc(LIMIT + 1)})],
    ['non-success HTTP status', call => call.reply({}, {status: 403})],
    ['pre-dispatch busy response', call => call.reply({error: 'Another memory request is active'})],
  ]) await test('HTTP ' + name + ' stops both transports with no replay', async () => {
    const web = fakeHttp(handler), sockets = fakeNet(deny);
    const transport = create(connection, {error: null}, {http: web, net: sockets});
    await assert.rejects(() => transport.rpc.request({command: 'synthetic'}));
    await stopped(transport, () => web.calls.length + sockets.calls.length);
  });

  await test('a short read stops before another chunk or cleanup request', async () => {
    const web = fakeHttp(call => call.reply({data: Buffer.alloc(call.body.length - 1).toString('base64')}));
    const sockets = fakeNet(deny), transport = create(connection, {error: null}, {http: web, net: sockets});
    await assert.rejects(() => transport.rpc.read(4294967296, 0x80001), /truncated/);
    await stopped(transport, () => web.calls.length + sockets.calls.length);
  });

  await test('malformed native call output shares the stop latch', async () => {
    const web = fakeHttp(call => call.reply({result: 0, output: ''}));
    const sockets = fakeNet(deny), transport = create(connection, {error: null}, {http: web, net: sockets});
    await assert.rejects(() => transport.rpc.callAddress6(4294967296, []), /Invalid native call reply/);
    await stopped(transport, () => web.calls.length + sockets.calls.length);
  });

  await test('retained endpoint uses one loopback request and preserves command framing', async () => {
    const sockets = fakeNet(call => {
      assert.deepEqual(call.target, {host: '127.0.0.1', port: 12346});
      assert.deepEqual(call.command, {command: 'synthetic', address: '4294967296'});
      call.reply({ready: true});
    });
    const transport = create(connection, {error: null}, {http: fakeHttp(deny), net: sockets});
    assert.deepEqual(await transport.endpointRequest('127.0.0.1:12346', {command: 'synthetic', address: 4294967296n}), {ready: true});
    assert.equal(sockets.calls.length, 1);
  });

  for (const [name, handler] of [
    ['busy', call => call.reply({error: 'Another memory request is active'})],
    ['short JSON', call => call.reply(null, {raw: Buffer.from('{')})],
    ['oversize', call => call.reply(null, {raw: Buffer.alloc(LIMIT + 1)})],
    ['disconnect', call => call.reply({}, {closeOnly: true})],
    ['timeout', call => call.socket.timeout()],
  ]) await test('endpoint ' + name + ' stops HTTP and endpoint cleanup with no retry', async () => {
    const sockets = fakeNet(handler), web = fakeHttp(deny);
    const transport = create(connection, {error: null}, {http: web, net: sockets});
    await assert.rejects(() => transport.endpointRequest('/synthetic/memory.sock', {command: 'synthetic'}));
    await stopped(transport, () => web.calls.length + sockets.calls.length);
  });

  await test('remote memory TCP endpoints are refused before connection', async () => {
    const sockets = fakeNet(deny), web = fakeHttp(deny);
    const transport = create(connection, {error: null}, {http: web, net: sockets});
    await assert.rejects(() => transport.endpointRequest('192.0.2.20:12346', {}), /loopback/);
    assert.equal(sockets.calls.length + web.calls.length, 0);
  });

  await test('invalid auth/port is rejected before any stream exists', () => {
    for (const invalid of [{...connection, token: 'short'}, {...connection, port: 0}, {...connection, port: 65536}])
      assert.throws(() => create(invalid, {}, {http: fakeHttp(deny), net: fakeNet(deny)}));
  });
  assert.equal(forbidden, 0);
  console.log(JSON.stringify({passed, forbiddenIO: forbidden, syntheticOnly: true}));
})().catch(error => { console.error(error.stack); process.exitCode = 1; });
