/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MOADER_RUNTIME_LOG_WIRE_H
#define MOADER_RUNTIME_LOG_WIRE_H
#include <stdint.h>
#include <stddef.h>

/* Independent little-endian diagnostic protocol. No process pointers. Files
 * are precreated at CAPACITY bytes; only payload_size bytes are valid text. */
#define MOADER_LOG_MAGIC "MOADERLG"
#define MOADER_LOG_VERSION 1u
#define MOADER_LOG_PAYLOAD (64u * 1024u)
#define MOADER_LOG_PATH0 "/moader/runtime/log0.bin"
#define MOADER_LOG_PATH1 "/moader/runtime/log1.bin"
struct MoaderLogHeader {
    char magic[8];
    uint32_t version, header_size;
    uint64_t epoch_low, epoch_high, generation, sequence, start_offset, end_offset;
    uint32_t payload_size, payload_crc, report_failed, header_crc;
};
#define MOADER_LOG_CAPACITY (80u + MOADER_LOG_PAYLOAD)
#ifdef __cplusplus
static_assert(sizeof(MoaderLogHeader) == 80);
#else
_Static_assert(sizeof(struct MoaderLogHeader) == 80, "Diagnostic snapshot ABI");
#endif
static inline uint32_t moader_log_crc32(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = ~0u;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
#endif
