// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const hosts = require('./host-session.cjs');

test('game attachment selects the held Seedsow source and endpoint before a native call', async () => {
  const base = fs.mkdtempSync(path.join(os.tmpdir(), 'moader-seedsow-game-'));
  const run = path.join(base, 'switch-Test');
  fs.mkdirSync(path.join(run, 'switch2/ro/root'), {recursive: true});
  fs.writeFileSync(path.join(run, 'moader-seedsow-session.json'), '{}');
  fs.writeFileSync(path.join(run, 'switch2/current-acquisition.json'),
    JSON.stringify({socketPath: path.join(run, 'owned.sock')}));
  fs.writeFileSync(path.join(run, 'switch2/ro/root/current-root.json'),
    JSON.stringify({rootHandle: 7}));
  const qualify = hosts.qualifyRun, bind = hosts.bind, load = hosts.load;
  const sentinel = Error('selected endpoint reached');
  const selected = [];
  hosts.qualifyRun = (file, id) => {
    assert.equal(file, run); assert.equal(id, 'seedsow-malev0');
    return {target: 'seedsow', run, profile: {id}};
  };
  hosts.bind = context => { selected.push('bind'); return context; };
  hosts.load = (_context, relative) => {
    selected.push(relative);
    if (relative === 'switch2/tools/endpoint.cjs') return {
      request(socket, command) {
        assert.equal(socket, path.join(run, 'owned.sock'));
        assert.deepEqual(command, {command: 'root-native-info'});
        throw sentinel;
      },
    };
    if (relative === 'switch2/app/session.cjs') return {Session: class {}, live: {session: 'seedsow'}};
    return {};
  };
  try {
    await assert.rejects(() =>
      require('../runtime/modloader/lib/game-files/session.cjs').attachGame(run),
    error => error === sentinel);
    assert.deepEqual(selected, ['bind', 'switch2/app/session.cjs',
      'switch2/app/domain.cjs', 'switch2/app/filesystem.cjs',
      'switch2/tools/endpoint.cjs']);
  } finally {
    hosts.qualifyRun = qualify; hosts.bind = bind; hosts.load = load;
    fs.rmSync(base, {recursive: true, force: true});
  }
});
