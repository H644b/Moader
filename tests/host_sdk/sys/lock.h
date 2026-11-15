#pragma once

// Host-only include shim for libounce's switch/kernel/mutex.h. The C++ test
// fixtures declare these newlib lock types before including SDK declarations.
// glibc has no sys/lock.h; using the target newlib include tree would replace
// host libc headers as well. Supply only this include surface and check the
// fixture types. Mutex operations and all other declarations remain the SDK's.
static_assert(sizeof(_LOCK_T) == 4, "Host SDK fixture lock must be 32 bits");
static_assert(sizeof(_LOCK_RECURSIVE_T) == 8,
              "Host SDK fixture recursive lock must contain two 32-bit fields");
static_assert(sizeof(((_LOCK_RECURSIVE_T*)nullptr)->lock) == 4);
static_assert(sizeof(((_LOCK_RECURSIVE_T*)nullptr)->counter) == 4);
