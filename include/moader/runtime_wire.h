/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MOADER_RUNTIME_WIRE_H
#define MOADER_RUNTIME_WIRE_H
#include <stdint.h>
#include <stddef.h>

/* Little-endian disk protocol. It contains offsets, never process pointers. */
#define MOADER_CATALOGUE_MAGIC "MOADER01"
#define MOADER_ACK_MAGIC "MOADERAK"
#define MOADER_WIRE_VERSION 1u
#define MOADER_MAX_ROWS 16384u
#define MOADER_MAX_CATALOGUE (8u * 1024u * 1024u)
struct MoaderCatalogueHeader {
    char magic[8];
    uint32_t version, rows;
    uint64_t generation, epoch_low, epoch_high;
    uint32_t strings_offset, total_size, payload_crc, reserved;
    uint64_t reserved2;
};
struct MoaderCatalogueRow {
    uint64_t title_id, size;
    uint32_t kind, name_offset, source_offset, reserved;
};
struct MoaderRuntimeAck {
    char magic[8];
    uint32_t version, result;
    uint64_t generation, epoch_low, epoch_high, reloads, retained_bytes;
    uint32_t flags, crc;
};
#if defined(__cplusplus)
static_assert(sizeof(MoaderCatalogueHeader) == 64);
static_assert(sizeof(MoaderCatalogueRow) == 32);
static_assert(sizeof(MoaderRuntimeAck) == 64);
#else
_Static_assert(sizeof(struct MoaderCatalogueHeader) == 64, "Catalogue header ABI");
_Static_assert(sizeof(struct MoaderCatalogueRow) == 32, "Catalogue record ABI");
_Static_assert(sizeof(struct MoaderRuntimeAck) == 64, "Runtime receipt ABI");
#endif
static inline uint32_t moader_crc32(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = ~0u;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
#endif
