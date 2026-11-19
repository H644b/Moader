/* SPDX-License-Identifier: GPL-2.0-only
 * Requests built by libnx's own CMIF encoder, as libnx ldrRo* sends them
 * (nx/source/services/ro.c _rosrvInitialize/_rosrvLoadNro/...: plain
 * Request, PID, payload starting with a PID placeholder), must decode.
 * The kernel's PID and copied-handle translation is simulated.
 */
#include <switch/sf/cmif.h>

#include "../source/wire.h"

#include <assert.h>
#include <string.h>

static uint32_t g_words[64];

static void* build(uint32_t command, const void* payload, uint32_t size, uint32_t handles) {
    memset(g_words, 0, sizeof g_words);
    CmifRequest req = cmifMakeRequest(g_words, (CmifRequestFormat){
                                                   .request_id = command,
                                                   .data_size = size,
                                                   .num_handles = handles,
                                                   .send_pid = 1,
                                               });
    memcpy(req.data, payload, size);
    /* Kernel: the PID slot (after the special header) and the copied
     * handle are rewritten for the server. */
    g_words[3] = 213;
    g_words[4] = 0;
    if (handles)
        req.hipc.copy_handles[0] = 0x42;
    return g_words;
}

int main(void) {
    RoRequest out;

    const uint64_t pid_placeholder = 0;
    build(RoCmd_Initialize, &pid_placeholder, sizeof pid_placeholder, 1);
    assert(g_words[0] == 4); /* libnx: Request, not RequestWithContext */
    assert(roParseRequest(g_words, &out));
    assert(out.cmd_id == RoCmd_Initialize && out.pid == 213 && out.process == 0x42 &&
           out.token == 0);

    const uint64_t load[5] = {0, 0x80000000, 0x5000, 0x80005000, 0x2000};
    build(RoCmd_LoadNro, load, sizeof load, 0);
    assert(roParseRequest(g_words, &out));
    assert(out.cmd_id == RoCmd_LoadNro && out.process == 0);
    assert(out.args[0] == 0x80000000 && out.args[1] == 0x5000 && out.args[2] == 0x80005000 &&
           out.args[3] == 0x2000);

    const uint64_t unload[2] = {0, 0x80000000};
    build(RoCmd_UnloadNro, unload, sizeof unload, 0);
    assert(roParseRequest(g_words, &out) && out.args[0] == 0x80000000);

    const uint64_t nrr[3] = {0, 0x90000000, 0x1000};
    build(RoCmd_LoadNrr, nrr, sizeof nrr, 0);
    assert(roParseRequest(g_words, &out) && out.args[0] == 0x90000000 && out.args[1] == 0x1000);
    build(RoCmd_UnloadNrr, unload, sizeof unload, 0);
    assert(roParseRequest(g_words, &out) && out.cmd_id == RoCmd_UnloadNrr);

    /* Short payload: rejected. */
    build(RoCmd_LoadNro, load, 16, 0);
    assert(!roParseRequest(g_words, &out));
    /* Wrong handle count for Initialize: rejected. */
    build(RoCmd_Initialize, &pid_placeholder, sizeof pid_placeholder, 0);
    assert(!roParseRequest(g_words, &out));

    /* Diagnostic replies carry the kernel's server PID descriptor and the
     * exact challenge data after the CMIF header. The host binds to that PID. */
    const uint64_t challenge[6] = {0x4558454f5232534eULL, 7, 213, 9, 11, 0};
    roWriteDiagnosticReply(g_words, 0, challenge, sizeof challenge);
    assert(g_words[0] == 0 && (g_words[1] & 0x80000000u));
    assert(g_words[2] == 1); /* Kernel PID flag, no copied handles. */
    const HipcParsedRequest reply = hipcParseRequest(g_words);
    const auto* header = static_cast<const CmifOutHeader*>(
        cmifGetAlignedDataStart(reply.data.data_words, g_words));
    assert(header->magic == CMIF_OUT_HEADER_MAGIC && header->result == 0);
    assert(!memcmp(header + 1, challenge, sizeof challenge));

    uint8_t oversized[185] = {};
    roWriteDiagnosticReply(g_words, 0, oversized, sizeof oversized);
    const HipcParsedRequest rejected = hipcParseRequest(g_words);
    const auto* rejection = static_cast<const CmifOutHeader*>(
        cmifGetAlignedDataStart(rejected.data.data_words, g_words));
    assert(rejection->result != 0);
    return 0;
}
