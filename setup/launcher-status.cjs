// SPDX-License-Identifier: GPL-2.0-only
'use strict';

// GET /status returns a help banner on current Seedsow saves. Both supported
// servers expose POST /eval; older launchers returned an eval-style status.
const banners = new Set([
  'n2h HTTP server (seedsow)',
  'seedsow-lua-editor-pro + switch-artemis-launcher HTTP server',
]);
module.exports = (statusCode, body) => {
  if (statusCode !== 200 || typeof body !== 'string') return false;
  if (/^status: ok(?:\r?\n|$)/.test(body)) return true;
  const lines = body.split(/\r?\n/);
  return banners.has(lines[0]) && lines.includes('POST /eval body=<lua>');
};
