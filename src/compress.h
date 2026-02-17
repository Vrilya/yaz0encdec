#ifndef COMPRESS_H
#define COMPRESS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Compress an uncompressed OoT ROM using Yaz0.
 * Uses the global entries[] table (must be populated via parse_dma_table first).
 *
 *   rom_data   - input ROM buffer
 *   mb         - target output size in MiB (0 = auto-align to 8 MiB boundary)
 *   dma_offset - byte offset of the DMA table
 *   dma_count  - number of DMA entries
 *   out_size   - receives the output ROM size
 *
 * Returns a newly allocated buffer with the compressed ROM.
 */
uint8_t *compress_rom(const uint8_t *rom_data, int mb,
                      uint32_t dma_offset, int dma_count,
                      size_t *out_size);

#endif /* COMPRESS_H */