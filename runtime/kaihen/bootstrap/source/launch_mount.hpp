// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "startup.hpp"

namespace ounce {

// Shared by the bootstrap and its host regression: only the gate/mount
// transports are substituted in the fixture, never the serving loop.
template <typename Launch, typename Gate, typename Mount>
inline void exchangeLaunchMount(Launch* args, Gate& gate, Mount& mount) {
    for (u32 step = 0; step < 30000 && !args->exchanged; step++) {
        bool ready = false;
        args->result = gate.pending(ready);
        if (R_FAILED(args->result)) {
            break;
        }
        if (!ready) {
            // Poll completion without calling into the worker's thread
            // runtime while this loop serves Loader's pending mount.
            if (__atomic_load_n(&args->pm.completed, __ATOMIC_ACQUIRE) &&
                R_FAILED(args->pm.result)) {
                break;
            }
            svcSleepThread(1000000);
            continue;
        }
        ounce::DomainEntry before[ounce::MaxDomainEntries], after[ounce::MaxDomainEntries], added;
        u32 before_count = 0, after_count = 0;
        args->mark(BootstrapOperation_Mount);
        args->result = mount.snapshot(before, before_count);
        if (R_FAILED(args->result)) {
            break;
        }
        alignas(16) u8 request[ounce::LoaderRequestSize];
        args->result = gate.step(request);
        if (R_FAILED(args->result)) {
            break;
        }
        args->result = mount.snapshot(after, after_count);
        if (R_FAILED(args->result)) {
            break;
        }
        bool target = false;
        args->result = mount.inspectRequest(request, target);
        if (R_FAILED(args->result)) {
            break;
        }
        if (target) {
            args->mounts++;
            args->result = mount.added(
                before, before_count, after, after_count, added);
            if (R_FAILED(args->result)) {
                break;
            }
            if (args->mounts == 2) {
                args->mark(BootstrapOperation_Exchange);
                args->result = mount.exchange(added);
                if (R_SUCCEEDED(args->result)) {
                    args->exchanged = true;
                }
                break;
            }
        }
    }
}


} // namespace ounce
