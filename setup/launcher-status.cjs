// SPDX-License-Identifier: GPL-2.0-only
'use strict';

// GET /status returns the launcher help banner on current Seedsow saves.
// Older launchers returned an eval-style status line instead.
module.exports = (statusCode, body) => statusCode === 200 &&
  (/^n2h HTTP server \(seedsow\)(?:\r?\n|$)/.test(body) ||
   /^status: ok(?:\r?\n|$)/.test(body));
