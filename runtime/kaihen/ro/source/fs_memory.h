// SPDX-License-Identifier: GPL-2.0-only
// RO's private ro:dmnt endpoint (../../common/ro_dmnt.h) and bounded
// standalone FS commands on the existing app-space ldr:ro session.
#pragma once

/// True only for the standalone adapter's reserved command IDs.
bool roIsStandaloneFsCommand(void* message);

/// Answers one request on an FS memory session. On app-space ldr:ro, only a
/// PID-bearing probe carrying a copied FS process handle is accepted.
void roFsMemoryDispatch(void* message, bool fs_only);
