// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Exercise the patched GUI's first auto-download with pinned public ZIP bytes
// and synthetic local Moader inputs. No console endpoint or native call is used.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const root = path.resolve(__dirname, '..');
const gui = path.resolve(process.argv[2] || '');
assert.ok(process.argv[2] && fs.statSync(gui).isDirectory(),
  'Pass the freshly provisioned Seedsow GUI checkout');
const official = fs.readFileSync(path.join(root, 'research/artifacts/kaiHEN-20261114c.zip'));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(sha(official),
  '63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967');

async function main() {
  const work = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'moader-gui-download-')));
  const oldFetch = global.fetch;
  const oldRoot = process.env.MOADER_ROOT;
  const oldNode = process.env.MOADER_NODE24;
  try {
    const moader = path.join(work, 'Moader kit with spaces');
    const inputs = path.join(moader, 'inputs/kaihen');
    fs.mkdirSync(path.join(moader, 'config'), {recursive: true});
    fs.mkdirSync(path.join(moader, 'tools'), {recursive: true});
    fs.mkdirSync(inputs, {recursive: true});
    fs.writeFileSync(path.join(moader, 'config/seedsow-provider.json'), JSON.stringify({
      id: 'seedsow-malev0', revision: '7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17',
      sourceTreeSha256: '4d4375f2729ca4b95a98348419c1e2b7edffa684bf53316f00a38194c5f4ca7b',
    }));

    const unzip = require(path.join(gui, 'tools/zip.cjs'));
    const baseDirectory = path.join(work, 'official');
    const archive = path.join(work, 'official.zip');
    fs.writeFileSync(archive, official);
    unzip.extract(archive, baseDirectory);
    const base = require(path.join(gui, 'tools/kaihen-release.cjs'))
      .findReleaseRoot(baseDirectory);
    const components = {};
    const kaihenFiles = [];
    for (const name of ['ro/main', 'hen/main', 'bootstrap.bin', 'game/main', 'hbmenu.nro']) {
      const bytes = ['ro/main', 'hen/main', 'bootstrap.bin'].includes(name)
        ? Buffer.from('matched fixture ' + name)
        : fs.readFileSync(path.join(base, name));
      const source = path.join(inputs, name);
      fs.mkdirSync(path.dirname(source), {recursive: true});
      fs.writeFileSync(source, bytes);
      const item = {source, bytes: bytes.length, sha256: sha(bytes)};
      kaihenFiles.push(item);
      if (['ro/main', 'hen/main', 'bootstrap.bin'].includes(name)) components[name] = item;
    }
    const programs = [];
    for (const [target, name] of [['/switch/Moader.nro', 'Moader.nro'],
      ['/kaihen/plugins/moader.nso', 'moader.nso']]) {
      const source = path.join(moader, 'dist', name);
      const bytes = Buffer.from('matched fixture ' + name);
      fs.mkdirSync(path.dirname(source), {recursive: true});
      fs.writeFileSync(source, bytes);
      programs.push({target, source, bytes: bytes.length, sha256: sha(bytes)});
    }
    const manifest = {hostProfile: {revision: '7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17'},
      kaihen: inputs, kaihenFiles, programs, manifestFile: path.join(work, 'fixture-manifest.json')};
    fs.writeFileSync(path.join(moader, 'tools/install-moader.cjs'),
      'module.exports.prepare=async()=>(' + JSON.stringify(manifest) + ');\n');
    fs.writeFileSync(path.join(moader, 'tools/install-inputs.cjs'),
      'module.exports.qualifyKaihenBuild=async()=>(' + JSON.stringify({
        schema: 3, protocolVersion: 7, components,
      }) + ');\n');

    process.env.MOADER_ROOT = moader;
    process.env.MOADER_NODE24 = process.execPath;
    let downloads = 0;
    global.fetch = async url => {
      assert.equal(url,
        'https://github.com/ouncebrew/kaihen/releases/download/20261114c/kaiHEN-20261114c.zip');
      downloads++;
      return {ok: true, arrayBuffer: async () => official};
    };
    const helper = require(path.join(gui, 'tools/moader-local.cjs'));
    const runs = path.join(gui, 'runs');
    assert.equal(fs.existsSync(runs), false, 'Fixture GUI must start without runs/');
    const result = await helper.updateFromMatchedBuild();
    assert.equal(downloads, 1);
    assert.equal(result.matched, true);
    assert.equal(fs.statSync(runs).isDirectory(), true);
    assert.equal(fs.statSync(result.backup).isDirectory(), true);
    helper.verifyBundle(manifest);
    for (const [name, item] of Object.entries(components))
      assert.equal(sha(fs.readFileSync(path.join(gui, 'kaihen', name))), item.sha256);
    console.log('PASS: fresh GUI auto-download stages a matched bundle without console contact');
  } finally {
    global.fetch = oldFetch;
    if (oldRoot === undefined) delete process.env.MOADER_ROOT;
    else process.env.MOADER_ROOT = oldRoot;
    if (oldNode === undefined) delete process.env.MOADER_NODE24;
    else process.env.MOADER_NODE24 = oldNode;
    fs.rmSync(work, {recursive: true, force: true});
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
