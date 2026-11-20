// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Keep the pre-listener Seedsow program gate tied to the pinned release bytes.
// This host test never opens a console transport.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {execFileSync} = require('node:child_process');
const {qualifyOfficialShell} = require('./seedsow-install.cjs');

const archive = path.resolve(__dirname, '../research/artifacts/kaiHEN-20261117.1.zip');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const base = fs.readFileSync(archive);
assert.equal(sha(base), '3f7ded0b360b2f0bf97f8e958c3bfe93a8d76dd8d43ce2e8f7d060e647ad0978');
const shellBytes = execFileSync('python3', ['-c',
  'import sys,zipfile\nwith zipfile.ZipFile(sys.argv[1]) as z:\n sys.stdout.buffer.write(z.read("programs/ounceshell.nro"))',
  archive], {maxBuffer: 32 * 1024 * 1024});
const shell = {target: '/switch/ounceshell.nro', bytes: shellBytes.length, sha256: sha(shellBytes)};

test('current pinned official OunceShell passes before any listener opens', () => {
  assert.equal(qualifyOfficialShell({bundledPrograms: [shell]}), shell);
});

test('old, absent, changed or duplicate OunceShell stops before native work', () => {
  for (const bundledPrograms of [
    [],
    [{...shell, sha256: '675b492a1922b3dafa7a69fe3afe37cda117ecdc406ae5fc964d093fc761c9ba'}],
    [{...shell, bytes: shell.bytes - 1}],
    [shell, shell],
  ]) assert.throws(() => qualifyOfficialShell({bundledPrograms}),
    /complete official kaiHEN program set/);
});
