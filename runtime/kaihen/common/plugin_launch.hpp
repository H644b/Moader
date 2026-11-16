// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "package.hpp"
#include "loader_gate.hpp"

// Borrowed only while GameArm's reload lease is held. No destructor closes
// these resources; the original GameArm remains their sole lifetime owner.
struct PluginLaunchContext {
    ounce::RetainedMemoryConfig memory;
    ounce::LoaderGateConfig gate;
    ounce::DomainMountConfig mount;
    Handle reader;
    Handle filesystem_process;
    Handle filesystem_root;
    Handle loader_root;
    Handle monitor;
};

struct PluginLaunchSpec {
    u64 program_id;
    u32 flags;
    u8 storage_id;
    u8 reserved[3];
};
static_assert(sizeof(PluginLaunchSpec) == 16);
