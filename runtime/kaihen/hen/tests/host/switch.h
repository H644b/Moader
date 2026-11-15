// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Test-only selector for the real SDK declarations used by HEN's IPC
// fixtures and the actual plugin server. The SDK umbrella also imports
// ARM-only crypto intrinsics, which these host tests neither use nor emulate.
extern "C" {
#include <switch/types.h>
#include <switch/result.h>
#include <switch/kernel/svc.h>
#include <switch/kernel/mutex.h>
#include <switch/kernel/thread.h>
#include <switch/arm/tls.h>
#include <switch/sf/hipc.h>
#include <switch/sf/cmif.h>
#include <switch/sf/service.h>
}
