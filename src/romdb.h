#ifndef ROMDB_H
#define ROMDB_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *name;
    const char *build_date;   /* ASCII, 17 chars */
    uint32_t    build_offset; /* where to find it in the ROM */
    uint32_t    dma_offset;
    int         dma_count;
    const int  *skip_indices; /* -1 terminated list */
} rom_version_t;

/* Number of entries in the rom_versions table */
extern const size_t num_rom_versions;

/* The full ROM version database */
extern const rom_version_t rom_versions[];

/*
 * Detect ROM version by matching the build date string.
 * Returns a pointer to the matching entry, or NULL if unknown.
 */
const rom_version_t *detect_rom_version(const uint8_t *rom_data, size_t rom_size);

/*
 * Mark all DMA entries for compression, then un-mark the skip list
 * for the given ROM version. Operates on the global entries[] table.
 */
void apply_rom_config(const rom_version_t *ver);

#endif /* ROMDB_H */
