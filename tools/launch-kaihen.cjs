#!/usr/bin/env node
// Fresh-boot acquisition using upstream's standard HEN launcher and the local patch.
const path = require('node:path');
const {spawn} = require('node:child_process');
const {isIP} = require('node:net');
const host = process.argv[2];
if (process.argv.length !== 3 || isIP(host || '') !== 4) {
  console.error('Usage: node tools/launch-kaihen.cjs YOUR_COMPUTER_LAN_IP');
  process.exit(2);
}
const root = path.resolve(__dirname, '..');
const child = spawn(process.execPath, [path.join(root, 'research/sources/ns2jb/hen.cjs'),
    '--dir', path.join(root, 'vendor/kaihen-mo'), host],
  {stdio: 'inherit', env: {...process.env, NS2_BASIC_ARITY: '5'}});
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => child.kill(signal));
child.once('exit', code => { process.exitCode = code === null ? 1 : code; });
child.once('error', error => { console.error(error.message); process.exitCode = 1; });
