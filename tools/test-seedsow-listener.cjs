// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const assert = require('node:assert/strict');
const {test} = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const net = require('node:net');
const http = require('node:http');
const listener = require('./seedsow-listener.cjs');
const provisioned = path.join(__dirname, '../research/sources/seedsow-fs-kaihen');
const MAGIC = 0x5248324e;
const sdk = 0x120000, main = 0x340000, scratch = 0x560000;
function reply(payload = Buffer.alloc(0), aux = 0n) {
  const head = Buffer.alloc(24);
  head.writeUInt32LE(MAGIC, 0);
  head.writeBigUInt64LE(BigInt(payload.length), 8);
  head.writeBigInt64LE(aux, 16);
  return Buffer.concat([head, payload]);
}
test('one launcher serve POST yields one owned binary socket and a fresh status challenge',
  {skip: !fs.existsSync(provisioned) && 'Pinned source not provisioned'}, async () => {
  const run = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'switch-')));
  let rpcServer, ownedSocket, servePost = 0, nativeCommands = [];
  const status = Buffer.from(`S\0${sdk}\0${main}\0${scratch}\0seedsow`);
  const reserve = net.createServer();
  await new Promise(resolve => reserve.listen(0, '127.0.0.1', resolve));
  const rpcPort = reserve.address().port;
  await new Promise(resolve => reserve.close(resolve));
  const web = http.createServer((request, response) => {
    assert.equal(request.url, '/eval');
    ++servePost;
    let source = '';
    request.on('data', part => { source += part; });
    request.on('end', () => {
      assert.match(source, /__rpc\.serve\(/);
      assert.doesNotMatch(source, /dofile\("save:\/n2h_rpc\.lua"\)/);
      assert.match(source, /assert\(loadstring\(\[=\[/);
      rpcServer = net.createServer(socket => {
        ownedSocket = socket;
        let input = Buffer.alloc(0);
        socket.on('data', part => {
          input = Buffer.concat([input, part]);
          while (input.length >= 64) {
            const length = input.readUInt32LE(48);
            if (input.length < 64 + length) return;
            const frame = input.subarray(0, 64 + length);
            input = input.subarray(64 + length);
            const command = frame.readUInt32LE(4);
            nativeCommands.push(command);
            if (command === 11) {
              // Make the HTTP serve completion race ahead of the TCP stop
              // reply; both orders must prove one stop without a false latch.
              response.end('status: ok\noutput:\nSERVE DONE conns=1 requests=4\n');
              setTimeout(() => { socket.write(reply()); socket.end(); }, 5);
            } else socket.write(command === 1 ? reply(status) : command === 10 ?
              reply(Buffer.alloc(0), frame.readBigUInt64LE(8)) : reply());
          }
        });
      });
      rpcServer.listen(rpcPort, '127.0.0.1');
      // The real /eval response remains parked until the serve loop ends.
      response.on('close', () => {});
    });
  });
  let result;
  try {
    await new Promise(resolve => web.listen(0, '127.0.0.1', resolve));
    const preflightFile = path.join(run, 'preflight.json');
    fs.writeFileSync(preflightFile, JSON.stringify({provider: 'seedsow-malev0', complete: true,
      console: '127.0.0.1', port: web.address().port, allRequiredPinnedSignaturesMatch: true,
      fsAcquisitionAttempted: false, codeOrKernelPatchAttempted: false,
      moduleBases: {sdk, main}}));
    result = await listener.open({host: '127.0.0.1', httpPort: web.address().port,
      rpcPort, run, preflightFile});
    assert.equal(servePost, 1);
    assert.deepEqual(nativeCommands, [1, 1, 10]);
    assert.equal(result.snapshot.addresses.scratch, scratch);
    assert.ok(result.snapshot.challenge > 0);
    assert.equal(result.snapshot.shimSha256,
      require('./seedsow-provider.cjs').resolveProfile(path.resolve(__dirname, '..')).files['console/n2h_rpc.lua']);
    assert.equal(result.guard.error, null);
    assert.ok(fs.statSync(result.connectionFile).isFile());
    assert.deepEqual(await result.stopAfterProvenSuccess(),
      {stopped: true, tcpClosed: true, serveEnded: true,
        response: path.join(run, 'moader-listener-response.json')});
    assert.deepEqual(nativeCommands, [1, 1, 10, 11]);
    assert.equal(result.guard.error, null);
  } finally {
    result?.socket.destroy(); result?.serveRequest.destroy(); ownedSocket?.destroy();
    if (rpcServer) await new Promise(resolve => rpcServer.close(resolve));
    await new Promise(resolve => web.close(resolve));
    fs.rmSync(run, {recursive: true, force: true});
  }
});

test('an early serve-loop reply is journaled and latches without acquisition or replay',
  {skip: !fs.existsSync(provisioned) && 'Pinned source not provisioned'}, async () => {
  const run = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'switch-')));
  const reserve = net.createServer();
  await new Promise(resolve => reserve.listen(0, '127.0.0.1', resolve));
  const rpcPort = reserve.address().port;
  await new Promise(resolve => reserve.close(resolve));
  let posts = 0;
  const web = http.createServer((_request, response) => {
    ++posts;
    response.end('status: ok\noutput:\nERR socket 23\n');
  });
  try {
    await new Promise(resolve => web.listen(0, '127.0.0.1', resolve));
    const preflightFile = path.join(run, 'preflight.json');
    fs.writeFileSync(preflightFile, JSON.stringify({provider: 'seedsow-malev0', complete: true,
      console: '127.0.0.1', port: web.address().port, allRequiredPinnedSignaturesMatch: true,
      fsAcquisitionAttempted: false, codeOrKernelPatchAttempted: false,
      moduleBases: {sdk, main}}));
    await assert.rejects(() => listener.open({host: '127.0.0.1', httpPort: web.address().port,
      rpcPort, run, preflightFile}), /ERR socket 23/);
    assert.equal(posts, 1);
    const evidence = JSON.parse(fs.readFileSync(path.join(run, 'moader-listener-response.json')));
    assert.equal(evidence.httpStatus, 200);
    assert.match(evidence.body, /ERR socket 23/);
    assert.equal(fs.existsSync(path.join(run, 'connection.json')), false);
  } finally {
    await new Promise(resolve => web.close(resolve));
    fs.rmSync(run, {recursive: true, force: true});
  }
});
