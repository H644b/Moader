// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
// Only the newlib lock layout needed by SDK headers in host IPC fixtures.
typedef uint32_t _LOCK_T;
typedef struct { uint32_t lock; uint32_t counter; } _LOCK_RECURSIVE_T;
