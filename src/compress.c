#include "compress.h"
#include "util.h"
#include "dma.h"
#include "yaz0.h"
#include "n64crc.h"

/* Sort comparator: by original start offset */
static int cmp_by_ostart(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    if (entries[ia].ostart < entries[ib].ostart) return -1;
    if (entries[ia].ostart > entries[ib].ostart) return 1;
    return 0;
}

uint8_t *compress_rom(const uint8_t *rom_data, int mb,
                      uint32_t dma_offset, int dma_count,
                      size_t *out_size) {
    for (int i = 0; i < num_entries; i++) {
        if (entries[i].deleted) {
            entries[i].start = entries[i].ostart;
            entries[i].end   = entries[i].oend;
            entries[i].compress = 0;
        }
    }

    for (int idx = 0; idx < num_entries; idx++) {
        dma_entry_t *e = &entries[idx];
        fprintf(stderr, "\rprocessing entry %d/%d: ", idx + 1, num_entries);
        fflush(stderr);

        if (e->start == e->end || e->deleted) continue;

        size_t file_size = e->end - e->start;
        const uint8_t *file_data = rom_data + e->start;

        if (!e->compress) {
            e->comp_data = (uint8_t *)malloc(file_size);
            if (!e->comp_data) die("out of memory");
            memcpy(e->comp_data, file_data, file_size);
            e->comp_sz = file_size;
            continue;
        }

        size_t comp_sz;
        uint8_t *comp = yaz0_encode(file_data, file_size, &comp_sz);

        if (comp_sz >= file_size) {
            free(comp);
            e->comp_data = (uint8_t *)malloc(file_size);
            if (!e->comp_data) die("out of memory");
            memcpy(e->comp_data, file_data, file_size);
            e->comp_sz = file_size;
            e->compress = 0;
        } else {
            e->comp_data = comp;
            e->comp_sz = comp_sz;
        }
    }
    fprintf(stderr, "\rprocessing entry %d/%d: success!\n", num_entries, num_entries);

    int sort_idx[MAX_DMA_ENTRIES];
    for (int i = 0; i < num_entries; i++) sort_idx[i] = i;
    qsort(sort_idx, num_entries, sizeof(int), cmp_by_ostart);

    size_t comp_total = 0, total_compressed = 0, total_decompressed = 0;
    for (int si = 0; si < num_entries; si++) {
        dma_entry_t *e = &entries[sort_idx[si]];
        if (e->deleted || e->comp_sz == 0) continue;
        size_t sz16 = align16(e->comp_sz);
        e->pstart = (uint32_t)comp_total;
        if (e->compress) {
            e->pend = (uint32_t)(e->pstart + sz16);
            total_compressed += sz16;
            total_decompressed += e->end - e->start;
        } else {
            e->pend = 0;
        }
        comp_total += sz16;
    }

    size_t compsz;
    if (mb == 0)
        compsz = align8mb(comp_total);
    else {
        compsz = (size_t)mb * 0x100000;
        if (comp_total > compsz) {
            fprintf(stderr, "error: compressed data (%.2f MiB) exceeds %d MiB limit\n",
                    (double)comp_total/(1024*1024), mb);
            exit(1);
        }
    }

    uint8_t *out_rom = (uint8_t *)calloc(compsz, 1);
    if (!out_rom) die("out of memory");

    int inject_total = 0;
    for (int i = 0; i < num_entries; i++)
        if (!entries[i].deleted && entries[i].comp_data) inject_total++;

    int inject_count = 0;
    for (int si = 0; si < num_entries; si++) {
        dma_entry_t *e = &entries[sort_idx[si]];
        if (e->deleted || !e->comp_data) continue;
        inject_count++;
        fprintf(stderr, "\rinjecting file %d/%d: ", inject_count, inject_total);
        fflush(stderr);
        memcpy(out_rom + e->pstart, e->comp_data, e->comp_sz);
        free(e->comp_data);
        e->comp_data = NULL;
    }
    fprintf(stderr, "\rinjecting file %d/%d: success!\n", inject_total, inject_total);

    if (total_decompressed > 0)
        fprintf(stderr, "compression ratio: %.2f%%\n",
                (double)total_compressed / (double)total_decompressed * 100.0);

    write_dma_table(out_rom, dma_offset, dma_count);
    n64crc(out_rom);
    *out_size = compsz;
    return out_rom;
}