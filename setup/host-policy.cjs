// SPDX-License-Identifier: GPL-2.0-only
'use strict';
const packagedPlatforms = new Set(['darwin-arm64', 'darwin-x64',
  'linux-arm64', 'linux-x64']);
function supported(method, platform, arch) {
  if (!packagedPlatforms.has(platform + '-' + arch)) return false;
  if (method === 'seedsow') return platform === 'darwin' && arch === 'arm64' ||
    platform === 'linux' && arch === 'x64';
  if (method === 'mac2jb') return platform === 'darwin';
  if (method === 'ns2jb' || method === 'civswitch')
    return platform === 'darwin' || platform === 'linux';
  return false;
}
module.exports = {supported, packagedPlatforms};
