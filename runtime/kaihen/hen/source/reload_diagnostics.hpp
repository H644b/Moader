// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../../libkaihen/include/kaihen/reload.h"

namespace reload {
inline void recordUncapturedPrimary(HenPluginReloadDiagnostics& diagnostics, Result result) {
    if (!(diagnostics.flags & HEN_RELOAD_DIAG_PRIMARY_CAPTURED)) diagnostics.primary_result = result;
}

// Shared by the worker's real fail path and focused host regressions.
inline void recordFailure(HenPluginReloadStatus& status, HenPluginReloadDiagnostics& diagnostics,
                          Result result, bool quarantined) {
    status.result = result;
    recordUncapturedPrimary(diagnostics, result);
    status.phase = quarantined ? HenPluginReloadPhase_Quarantined : HenPluginReloadPhase_Failed;
    if (quarantined) status.flags |= HEN_PLUGIN_RELOAD_BLOCKED;
}

// Pure snapshot publication. Probe is a previously cached local gate read;
// this helper never calls the gate, RO, FS, a clock or a logger.
template <typename Probe>
void captureGateDiagnostics(HenPluginReloadDiagnostics& diagnostics, Result result,
                            bool restoration, u32 step, const Probe& probe,
                            u32 original_wait, u32 park_wait) {
    if (restoration) {
        diagnostics.restoration_gate_step = step;
        diagnostics.restoration_result = result;
        diagnostics.flags |= HEN_RELOAD_DIAG_RESTORE_ATTEMPTED;
    } else {
        diagnostics.primary_gate_step = step;
        diagnostics.primary_result = result;
        diagnostics.flags |= HEN_RELOAD_DIAG_PRIMARY_CAPTURED;
    }
    diagnostics.probe_result = probe.read_result;
    diagnostics.probe_state = probe.state;
    diagnostics.probe_receive_result = probe.receive_result;
    diagnostics.probe_selected_mask = (probe.waiter_mask >> 8) & 0x7f;
    diagnostics.probe_wait_kind = probe.wait == original_wait ? 1 : probe.wait == park_wait ? 2 : probe.wait ? 3 : 0;
    diagnostics.flags &= ~HEN_RELOAD_DIAG_PROBE_VALID;
    if (R_SUCCEEDED(probe.read_result)) diagnostics.flags |= HEN_RELOAD_DIAG_PROBE_VALID;
}
} // namespace reload
