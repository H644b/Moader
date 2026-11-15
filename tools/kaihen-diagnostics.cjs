#!/usr/bin/env node
// Reconnect only SmileBASIC's application RPC. Never reacquire FS or replace RO.
const fs = require('node:fs/promises');
const path = require('node:path');
const {isIP} = require('node:net');
const args=process.argv.slice(2),profileAt=args.indexOf('--host-profile');
if(profileAt>=0){
 if(args[profileAt+1]!=='smilebasic-kurumuz'){
  console.error('This listener only reconnects SmileBASIC. For Civ7 keep the qualified held FS run open and use tools/inspect-hen.cjs RUN or tools/control-resident.cjs RUN error. No Civ7 acquisition or reconnect is performed.');process.exit(2);
 }
 args.splice(profileAt,2);
}
if(args.length===1&&args[0]==='--help'){console.log('SmileBASIC application-only reconnect: node tools/kaihen-diagnostics.cjs YOUR_COMPUTER_LAN_IP. Civ7 uses the existing held run with inspect-hen/control-resident.');process.exit(0);}
const host = args[0];
if (args.length !== 1 || isIP(host || '') !== 4) {
  console.error('Usage: node tools/kaihen-diagnostics.cjs YOUR_COMPUTER_LAN_IP');
  process.exit(2);
}
const root = path.resolve(__dirname, '..');
const engine = path.join(root, 'research/sources/ns2jb/engine');
const {listen} = require(path.join(engine, 'lib/connection.cjs'));
const {NativeClient} = require(path.join(engine, 'lib/native.cjs'));
const {qualify} = require(path.join(engine, 'lib/qualify.cjs'));
const {serve} = require(path.join(engine, 'lib/bridge.cjs'));
const save = (file, value) => fs.writeFile(file, JSON.stringify(value, null, 2) + '\n', {mode: 0o600});
async function main() {
  const directory = await fs.mkdtemp(path.join(root, 'evidence/kaihen/diagnostic-'));
  process.env.NS2_RUN_DIR = directory;
  await fs.mkdir(path.join(directory, 'switch2/app'), {recursive: true});
  const record = {started: new Date().toISOString(), run: directory, fsExploit: false};
  await save(path.join(root, 'evidence/kaihen/current-diagnostic.json'), record);
  const connected = await listen({host, port: 39380, onWaiting() {
    console.log('Open SmileBASIC at the direct prompt, then enter separately:');
    console.log('LOAD "TXT:NS2"\nRUN\nREMOVE NSR,&H60000000,&H3FFFFFFF');
    console.log(`V=START(39380,${host.split('.').join(',')})`);
    console.log('If START needs nine arguments, append ,0,0,0,0.');
    console.log('Application-only diagnostic listener: ' + directory);
  }});
  const native = new NativeClient(connected.connection, connected.addresses);
  let bridge, session;
  try {
    record.bootstrap = await qualify(native);
    bridge = await serve(native);
    bridge.keepAwake(record.bootstrap.activity);
    await save(path.join(directory, 'connection.json'), {port: bridge.port,
      addresses: {...connected.addresses, session: path.basename(directory), target: 'switch2'}});
    const {Session, check} = require(path.join(engine, 'switch2/app/session.cjs'));
    const {argumentsWithPid} = require(path.join(engine, 'switch2/ro/nro.cjs'));
    session = new Session('hen-status');
    await session.initialize();
    const ro = await session.openService('ldr:ro');
    check(!(await session.ipc('RO Initialize', ro, 4, argumentsWithPid(),
      {pid: true, copies: [0xffff8001]})).result, 'RO Initialize failed');
    const protocol = await fs.readFile(path.join(root, 'runtime/kaihen/libkaihen/include/kaihen/protocol.h'), 'utf8');
    const id = Number(protocol.match(/KaihenRoCommand_GetHomebrewSession\s*=\s*(0x[0-9a-f]+|\d+)/i)?.[1]);
    check(Number.isInteger(id), 'Homebrew-session command missing');
    const opened = await session.ipc('Open HEN session', ro, id, Buffer.alloc(8), {pid: true});
    check(!opened.result && opened.copies.length === 1, 'Cannot open HEN session');
    const hen = opened.copies[0];
    const status = await session.ipc('Read HEN startup status', hen, 0);
    const game = await session.ipc('Read HEN game-arm status', hen, 8);
    check(!status.result && !game.result, 'HEN status query failed');
    record.hen = {pid: status.data.readBigUInt64LE(0).toString(), protocol: status.data.readUInt32LE(8),
      result: status.data.readUInt32LE(12)};
    record.game = {phase: game.data.readUInt32LE(0), result: game.data.readUInt32LE(4),
      serial: game.data.readUInt32LE(8), mounts: game.data.readUInt32LE(12),
      pid: game.data.readBigUInt64LE(16).toString()};
    record.ready = true;
    await save(path.join(directory, 'status.json'), record);
    console.log(JSON.stringify({hen: record.hen, game: record.game, directory}, null, 2));
    await session.cleanup(); session = null;
    console.log('Diagnostic RPC remains open; keep SmileBASIC open.');
    await new Promise(resolve => {native.connection.socket.once('close', resolve);
      process.once('SIGINT', resolve); process.once('SIGTERM', resolve);});
  } catch (e) {
    record.error = e.stack;
    await save(path.join(directory, 'status.json'), record);
    throw e;
  } finally {
    if (session && !native.connection.error) await session.cleanup();
    if (bridge) await bridge.close();
    await native.stop();
  }
}
main().catch(e => {console.error(e.stack); process.exitCode = 1;});
