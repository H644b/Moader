// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const gui = path.join(root, 'build/setup-seedsow-gui');
process.env.MOADER_ROOT = root;
process.env.MOADER_NODE24 = process.execPath;
async function main() {
  const action = process.argv[2];
  const local = require(path.join(gui, 'tools/moader-local.cjs'));
  if (action === 'stage') {
    const result = await local.updateFromMatchedBuild();
    console.log(JSON.stringify({event: 'bundle-staged', result}));
  } else if (action === 'check') {
    const result = await local.prepare({requireBundle: true});
    console.log(JSON.stringify({event: 'bundle-checked', manifest: result.manifest.manifestFile}));
  } else throw Error('Unknown Seedsow bridge action');
}
main().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
