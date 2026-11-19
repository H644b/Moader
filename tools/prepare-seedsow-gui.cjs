#!/usr/bin/env node
// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Apply Moader's GUI integration only to the reviewed Seedsow revision.
// This command changes host files only; it does not contact the console.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {execFileSync} = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const PATCH = path.join(ROOT, 'integration/seedsow-gui.patch');
const REVISION = '7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17';
const OFFICIAL = 'https://github.com/Malev0/seedsow-fs-kaihen.git';
const PATCH_SHA256 = '3d90016b1bdedc084ca8c74835b7255904a968703665fb0e6005f722b5853014';
const FILES = Object.freeze({
  'gui/ns2-gui.cjs': {
    before: '8818822df303f940e44414faafcd199cf064a6e7a24fb80ac56fbdbd40d7c35c',
    after: '40231bc0e8050c93f65df610ebd58da463719d2de22ac349d46baac14259ae0c',
  },
  'gui/ns2-gui.html': {
    before: '50ea6dceeee52c8309d96d3b3f4fcea5acfed33e1bff3cae1ddc62dcb765880d',
    after: '6d67bb9a020060d5e52efe04bbfaeb13a36f7631d51c584fa01bf91e75bc84f3',
  },
  'tools/moader-local.cjs': {
    before: null,
    after: '2f13398da83efe785acf012b6d11c0988042c68d5b75a3693d28327e5bd9ff32',
  },
});
const check = (ok, message) => { if (!ok) throw Error(message); };
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function git(gui, ...args) {
  return execFileSync('git', ['-C', gui, ...args], {encoding: 'utf8', timeout: 10000}).trim();
}
function fileSha(gui, relative) {
  const file = path.join(gui, relative);
  if (!fs.existsSync(file)) return null;
  check(fs.realpathSync(file) === file && fs.lstatSync(file).isFile(),
    'Seedsow GUI target is not a canonical regular file: ' + relative);
  return sha(fs.readFileSync(file));
}
function qualify(gui) {
  check(path.isAbsolute(gui) && fs.realpathSync(gui) === gui && fs.lstatSync(gui).isDirectory(),
    'Seedsow GUI checkout must be a canonical absolute directory');
  check(git(gui, 'rev-parse', 'HEAD') === REVISION &&
    git(gui, 'rev-parse', '--show-toplevel') === gui,
  'Seedsow GUI checkout must be at the reviewed upstream revision');
  check(fileSha(ROOT, 'integration/seedsow-gui.patch') === PATCH_SHA256,
    'Moader Seedsow GUI patch changed');
  const changed = git(gui, 'diff', 'HEAD', '--name-only').split('\n').filter(Boolean);
  check(changed.every(name => Object.hasOwn(FILES, name) || name === 'kaihen/.gitkeep'),
    'Seedsow GUI has unrelated tracked changes');
  const hashes = Object.fromEntries(Object.keys(FILES).map(name => [name, fileSha(gui, name)]));
  const before = Object.entries(FILES).every(([name, pin]) => hashes[name] === pin.before);
  const after = Object.entries(FILES).every(([name, pin]) => hashes[name] === pin.after);
  check(before || after, 'Seedsow GUI files differ from both the reviewed base and Moader patch');
  return {gui, before, after, hashes};
}
function prepare(gui) {
  const selected = qualify(gui);
  if (selected.before) {
    execFileSync('git', ['-C', gui, 'apply', '--check', '--whitespace=error', PATCH],
      {timeout: 10000, stdio: 'pipe'});
    execFileSync('git', ['-C', gui, 'apply', '--whitespace=error', PATCH],
      {timeout: 10000, stdio: 'pipe'});
  }
  check(qualify(gui).after, 'Seedsow GUI patch output differs from the reviewed files');
  return gui;
}
function provision(destination, source = OFFICIAL) {
  destination = path.resolve(destination);
  const parent = path.dirname(destination);
  check(fs.realpathSync(parent) === parent && fs.lstatSync(parent).isDirectory(),
    'Seedsow GUI destination parent must be a canonical directory');
  check(!fs.existsSync(destination) && !fs.lstatSync(destination, {throwIfNoEntry: false}),
    'Seedsow GUI destination already exists');
  check(source === OFFICIAL || typeof source === 'string' && path.isAbsolute(source) &&
    fs.realpathSync(source) === source && fs.lstatSync(source).isDirectory(),
  'Seedsow GUI source must be the official repository or a canonical local checkout');
  const staging = fs.mkdtempSync(path.join(parent, '.moader-seedsow-gui-'));
  try {
    execFileSync('git', ['clone', '--quiet', '--no-checkout', source, staging],
      {timeout: 180000, stdio: 'pipe'});
    execFileSync('git', ['-C', staging, 'checkout', '--quiet', '--detach', REVISION],
      {timeout: 30000, stdio: 'pipe'});
    prepare(staging);
    fs.renameSync(staging, destination);
    check(qualify(destination).after, 'Provisioned Seedsow GUI changed after placement');
    return destination;
  } finally {
    if (fs.existsSync(staging)) fs.rmSync(staging, {recursive: true, force: true});
  }
}
function main(argv = process.argv.slice(2)) {
  check(argv.length >= 2 && argv.length <= 3 &&
    ['--prepare', '--check', '--provision'].includes(argv[0]) &&
    (argv[0] === '--provision' || argv.length === 2),
  'Usage: node tools/prepare-seedsow-gui.cjs --provision DESTINATION [LOCAL_GIT_SOURCE] | --prepare|--check GUI_CHECKOUT');
  const gui = argv[0] === '--provision' ? provision(argv[1], argv[2])
    : argv[0] === '--prepare' ? prepare(path.resolve(argv[1])) : path.resolve(argv[1]);
  if (argv[0] === '--check') check(qualify(gui).after,
    'Seedsow GUI integration has not been applied');
  const result = {schema: 1, scope: 'host files only', gui,
    upstreamRevision: REVISION, patchSha256: PATCH_SHA256, prepared: true};
  process.stdout.write(JSON.stringify(result, null, 2) + '\n');
  return result;
}
module.exports = {main, qualify, prepare, provision, REVISION, PATCH_SHA256};
if (require.main === module) {
  try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
