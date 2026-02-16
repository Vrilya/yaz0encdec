#include "decompress.h"
#include "util.h"
#include "yaz0.h"
#include "n64crc.h"
#include "dma.h"
#include "romdb.h"

uint8_t *do_decompress_rom(const uint8_t *comp, size_t comp_size, size_t *out_size) {
    const rom_version_t *ver = detect_rom_version(comp, comp_size);
    if (!ver) {
        fprintf(stderr,
            "error: could not identify ROM version.\n"
            "Supported versions:\n");
        for (size_t i = 0; i < num_rom_versions; i++)
            fprintf(stderr, "  %-22s  build: %s  @ 0x%X\n",
                    rom_versions[i].name, rom_versions[i].build_date,
                    rom_versions[i].build_offset);
        exit(1);
    }

    size_t dma_start = ver->dma_offset;
    int dma_num = ver->dma_count;

    fprintf(stderr, "detected: %s\n", ver->name);
    fprintf(stderr, "dmadata at 0x%X with %d entries\n",
            (unsigned)dma_start, dma_num);

    /* Determine decompressed size (scan vend values) */
    size_t dst_size = comp_size;
    for (int i = 0; i < dma_num; i++) {
        uint32_t vend = get32(comp, dma_start + (size_t)i * 16 + 4);
        if (vend > dst_size)
            dst_size *= 2;
    }

    uint8_t *dec = (uint8_t *)calloc(dst_size, 1);
    if (!dec) die("out of memory");

    int decomp_count = 0, copy_count = 0;

    /* Process each DMA entry */
    for (int i = 0; i < dma_num; i++) {
        size_t eofs = dma_start + (size_t)i * 16;

        uint32_t vstart = get32(comp, eofs);
        uint32_t vend   = get32(comp, eofs + 4);
        uint32_t pstart = get32(comp, eofs + 8);
        uint32_t pend   = get32(comp, eofs + 12);

        /* Skip invalid/deleted entries */
        if (pstart == DMA_DELETED || vstart == DMA_DELETED ||
            pend == DMA_DELETED || vend == DMA_DELETED ||
            vend <= vstart || (pend && pend == pstart))
            continue;

        fprintf(stderr, "\rdecompressing entry %d/%d ", i + 1, dma_num);
        fflush(stderr);

        if (pend != 0) {
            /* Compressed file */
            uint32_t sz = pend - pstart;
            yaz0_decode(comp, pstart, sz, dec, vstart);
            decomp_count++;
        } else {
            /* Uncompressed - straight copy */
            uint32_t size = vend - vstart;
            memcpy(dec + vstart, comp + pstart, size);
            copy_count++;
        }
    }
    fprintf(stderr, "\rdecompressing entry %d/%d: done!    \n", dma_num, dma_num);
    fprintf(stderr, "decompressed %d files, copied %d uncompressed files\n",
            decomp_count, copy_count);

    /* Build updated DMA table in dec: pstart=vstart, pend=0 */
    for (int i = 0; i < dma_num; i++) {
        size_t eofs = dma_start + (size_t)i * 16;

        uint32_t vstart = get32(comp, eofs);
        uint32_t vend   = get32(comp, eofs + 4);
        uint32_t pstart = get32(comp, eofs + 8);
        uint32_t pend   = get32(comp, eofs + 12);

        if (pstart == DMA_DELETED || vstart == DMA_DELETED ||
            pend == DMA_DELETED || vend == DMA_DELETED ||
            vend <= vstart || (pend && pend == pstart)) {
            /* Preserve entry as-is */
            put32(dec, dma_start + (size_t)i * 16,      vstart);
            put32(dec, dma_start + (size_t)i * 16 + 4,  vend);
            put32(dec, dma_start + (size_t)i * 16 + 8,  pstart);
            put32(dec, dma_start + (size_t)i * 16 + 12, pend);
        } else {
            put32(dec, dma_start + (size_t)i * 16,      vstart);
            put32(dec, dma_start + (size_t)i * 16 + 4,  vend);
            put32(dec, dma_start + (size_t)i * 16 + 8,  vstart);
            put32(dec, dma_start + (size_t)i * 16 + 12, 0);
        }
    }

    /* Update CRC */
    n64crc(dec);

    *out_size = dst_size;
    return dec;
}