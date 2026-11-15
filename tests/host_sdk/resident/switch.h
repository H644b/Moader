#pragma once

// Host lifecycle test umbrella: select the real SDK declarations used by the
// resident and kaiHEN plugin interface. The full SDK switch.h also imports
// unrelated ARM-only crypto intrinsics. Do not synthesize those for the host.
extern "C" {
#include <switch/types.h>
#include <switch/result.h>
#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
#include <switch/kernel/random.h>
#include <switch/services/applet.h>
#include <switch/services/fs.h>
#include <switch/services/nifm.h>
#include <switch/services/ssl.h>
#include <switch/runtime/devices/fs_dev.h>
#include <switch/runtime/devices/socket.h>
}
