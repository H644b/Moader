#pragma once
// Parse the owning SDK declarations while keeping host POSIX socket headers.
#include <cstdint>
typedef uint32_t _LOCK_T;
typedef struct { uint32_t lock, counter; } _LOCK_RECURSIVE_T;
extern "C" {
#include <switch/result.h>
#include <switch/kernel/thread.h>
}
