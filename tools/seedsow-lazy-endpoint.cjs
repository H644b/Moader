// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// The endpoint socket does not exist until the single acquisition succeeds.
// Bind exactly once, then share its guard with the console RPC and every
// source-owned module. An unbound request is a programming error and stops the
// run before any native endpoint operation can be dispatched.
const endpoint = require('./seedsow-endpoint.cjs');
const check = (ok, message) => { if (!ok) throw Error(message); };
function create(guard) {
  check(guard && typeof guard.fail === 'function', 'Seedsow lazy endpoint requires the run guard');
  let owner = null;
  function bind(socketPath) {
    check(!owner, 'Seedsow retained endpoint is already bound');
    try { owner = endpoint.create({socketPath, guard}); }
    catch (error) { throw guard.fail(error); }
    return owner;
  }
  function requireOwner() {
    if (guard.error) throw guard.error;
    check(owner, 'Seedsow retained endpoint is not yet owned');
    return owner;
  }
  return Object.freeze({bind, guard,
    get socketPath() { return owner?.socketPath; },
    get socketIdentity() { return owner?.socketIdentity; },
    request(command, timeoutMs) { return requireOwner().request(command, timeoutMs); },
    retireOnce(timeoutMs) { return requireOwner().retireOnce(timeoutMs); },
    read(address, length, run) { return requireOwner().read(address, length, run); },
  });
}
module.exports = {create};
