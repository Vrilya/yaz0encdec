#include "dma.h"
#include "util.h"

dma_entry_t entries[MAX_DMA_ENTRIES];
int          num_entries = 0;

void parse_dma_table(const uint8_t *rom_data, uint32_t offset, int count) {
    if (count > MAX_DMA_ENTRIES) die("too many DMA entries");
    num_entries = count;

    for (int i = 0; i < count; i++) {
        size_t raw_ofs = offset + (size_t)i * 16;
        dma_entry_t *e = &entries[i];
        e->index  = i;
        e->start  = get32(rom_data, raw_ofs);
        e->end    = get32(rom_data, raw_ofs + 4);
        e->pstart = get32(rom_data, raw_ofs + 8);
        e->pend   = get32(rom_data, raw_ofs + 12);
        e->ostart = e->start;
        e->oend   = e->end;
        e->compress  = 0;
        e->deleted   = 0;
        e->comp_data = NULL;
        e->comp_sz   = 0;

        if (e->pstart == DMA_DELETED && e->pend == DMA_DELETED) {
            e->deleted = 1;
        } else if (e->pend != 0 && e->pend != DMA_DELETED) {
            fprintf(stderr, "error: DMA entry %d (%08X %08X %08X %08X) "
                    "suggests the ROM is already compressed\n",
                    i, e->start, e->end, e->pstart, e->pend);
            exit(1);
        }
    }
}

void validate_dma(size_t rom_size) {
    int idx[MAX_DMA_ENTRIES];
    int n = 0;
    for (int i = 0; i < num_entries; i++)
        if (entries[i].start != 0 || entries[i].end != 0)
            idx[n++] = i;

    for (int i = 1; i < n; i++) {
        int key = idx[i]; int j = i - 1;
        while (j >= 0 && entries[idx[j]].start > entries[key].start)
            { idx[j+1] = idx[j]; j--; }
        idx[j+1] = key;
    }

    uint32_t lowest = 0;
    for (int i = 0; i < n; i++) {
        dma_entry_t *e = &entries[idx[i]];
        if (e->deleted) continue;
        if (e->end < e->start) die("DMA invalid entry");
        if ((e->start & 3) || (e->end & 3)) die("DMA unaligned pointer");
        if (e->end > rom_size) die("DMA entry exceeds ROM size");
        if (e->start < lowest) die("DMA entry overlaps previous");
        lowest = e->end;
    }
}

void write_dma_table(uint8_t *out, uint32_t dma_offset, int dma_count) {
    memset(out + dma_offset, 0, (size_t)dma_count * 16);

    for (int i = 0; i < num_entries; i++) {
        size_t ofs = dma_offset + (size_t)i * 16;
        dma_entry_t *e = &entries[i];
        put32(out, ofs, e->start); put32(out, ofs+4, e->end);
        put32(out, ofs+8, e->pstart); put32(out, ofs+12, e->pend);
    }
}
