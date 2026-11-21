// SPDX-License-Identifier: GPL-2.0-only
// RO's private ro:dmnt endpoint (../../common/ro_dmnt.h) and app-space
// mfs:ro endpoint: FS process memory access for HEN and Seedsow.
#pragma once

/// Answers one request on an FS memory session. In app space, only a
/// PID-bearing probe carrying the current FS process handle is accepted.
void roFsMemoryDispatch(void* message, bool fs_only);
