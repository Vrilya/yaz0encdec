#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stddef.h>

#define DMA_DELETED      0xFFFFFFFFu
#define MAX_DMA_ENTRIES  8192

typedef struct {
    int      index;
    uint32_t start, end;
    uint32_t pstart, pend;
    uint32_t ostart, oend;
    int      compress;
    int      deleted;
    uint8_t *comp_data;
    size_t   comp_sz;
} dma_entry_t;

/* Global DMA entry table (populated by parse_dma_table) */
extern dma_entry_t entries[MAX_DMA_ENTRIES];
extern int          num_entries;

/*
 * Parse DMA table from ROM data.
 *   offset   - byte offset of the DMA table in the ROM
 *   count    - number of entries
 */
void parse_dma_table(const uint8_t *rom_data, uint32_t offset, int count);

/* Validate DMA table entries for consistency */
void validate_dma(size_t rom_size);

/* Write the DMA table into the output ROM buffer */
void write_dma_table(uint8_t *out, uint32_t dma_offset, int dma_count);

#endif /* DMA_H */