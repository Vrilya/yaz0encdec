#include "deflate.h"

#include <string.h>

#define DEF_MAX_BITS 15
#define DEF_TABLE_BITS 15
#define DEF_TABLE_SIZE (1u << DEF_TABLE_BITS)
#define DEF_TABLE_EMPTY 0u

static const int len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10,
    11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115,
    131, 163, 195, 227, 258
};

static const int len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0
};

static const int dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13,
    17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073,
    4097, 6145, 8193, 12289, 16385, 24577
};

static const int dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,
    3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13
};

static const int codelen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

typedef struct {
    const uint8_t *src;
    size_t src_size;
    size_t pos;
    uint64_t bitbuf;
    unsigned bitcount;
} bit_reader_t;

typedef struct {
    uint16_t table[DEF_TABLE_SIZE];
} huff_t;

static uint32_t reverse_bits(uint32_t value, int count) {
    uint32_t out = 0;
    int i;
    for (i = 0; i < count; i++) {
        out = (out << 1) | (value & 1u);
        value >>= 1;
    }
    return out;
}

static void br_init(bit_reader_t *br, const uint8_t *src, size_t src_size) {
    br->src = src;
    br->src_size = src_size;
    br->pos = 0;
    br->bitbuf = 0;
    br->bitcount = 0;
}

static void br_fill(bit_reader_t *br, unsigned want) {
    while (br->bitcount < want && br->pos < br->src_size) {
        br->bitbuf |= (uint64_t)br->src[br->pos++] << br->bitcount;
        br->bitcount += 8;
    }
}

static int br_read(bit_reader_t *br, unsigned count, uint32_t *out) {
    uint64_t mask;
    if (count == 0) {
        *out = 0;
        return 1;
    }
    br_fill(br, count);
    if (br->bitcount < count)
        return 0;
    mask = ((uint64_t)1 << count) - 1;
    *out = (uint32_t)(br->bitbuf & mask);
    br->bitbuf >>= count;
    br->bitcount -= count;
    return 1;
}

static void br_align_byte(bit_reader_t *br) {
    unsigned drop = br->bitcount & 7u;
    br->bitbuf >>= drop;
    br->bitcount -= drop;
}

static int huff_build(huff_t *h, const uint8_t *lengths, int n) {
    int bl_count[DEF_MAX_BITS + 1];
    int next_code[DEF_MAX_BITS + 1];
    int code = 0;
    int bits;
    int symbol;

    memset(h->table, 0, sizeof(h->table));
    memset(bl_count, 0, sizeof(bl_count));
    memset(next_code, 0, sizeof(next_code));

    for (symbol = 0; symbol < n; symbol++) {
        if (lengths[symbol] > DEF_MAX_BITS)
            return 0;
        if (lengths[symbol])
            bl_count[lengths[symbol]]++;
    }

    for (bits = 1; bits <= DEF_MAX_BITS; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    for (symbol = 0; symbol < n; symbol++) {
        int len = lengths[symbol];
        uint32_t rev;
        uint32_t step;
        uint32_t idx;
        uint16_t packed;
        if (!len)
            continue;
        rev = reverse_bits((uint32_t)next_code[len], len);
        next_code[len]++;
        step = 1u << len;
        packed = (uint16_t)(((symbol + 1) << 4) | len);
        for (idx = rev; idx < DEF_TABLE_SIZE; idx += step)
            h->table[idx] = packed;
    }
    return 1;
}

static int huff_read_symbol(bit_reader_t *br, const huff_t *h, int *symbol) {
    uint32_t idx;
    uint16_t packed;
    unsigned len;

    br_fill(br, DEF_TABLE_BITS);
    idx = (uint32_t)(br->bitbuf & (DEF_TABLE_SIZE - 1u));
    packed = h->table[idx];
    if (packed == DEF_TABLE_EMPTY)
        return 0;
    len = packed & 15u;
    if (len == 0)
        return 0;
    if (br->bitcount < len)
        return 0;
    br->bitbuf >>= len;
    br->bitcount -= len;
    *symbol = (int)(packed >> 4) - 1;
    return 1;
}

static int fixed_tables(huff_t *lit, huff_t *dist) {
    uint8_t lit_lengths[288];
    uint8_t dist_lengths[32];
    int i;

    memset(lit_lengths, 0, sizeof(lit_lengths));
    for (i = 0; i < 144; i++) lit_lengths[i] = 8;
    for (i = 144; i < 256; i++) lit_lengths[i] = 9;
    for (i = 256; i < 280; i++) lit_lengths[i] = 7;
    for (i = 280; i < 288; i++) lit_lengths[i] = 8;
    for (i = 0; i < 32; i++) dist_lengths[i] = 5;

    return huff_build(lit, lit_lengths, 288) &&
           huff_build(dist, dist_lengths, 32);
}

static int dynamic_tables(bit_reader_t *br, huff_t *lit, huff_t *dist) {
    uint32_t v;
    int hlit, hdist, hclen;
    uint8_t code_lengths[19];
    uint8_t lengths[286 + 32];
    huff_t code_table;
    int i;
    int total;
    int used = 0;

    if (!br_read(br, 5, &v)) return 0;
    hlit = (int)v + 257;
    if (!br_read(br, 5, &v)) return 0;
    hdist = (int)v + 1;
    if (!br_read(br, 4, &v)) return 0;
    hclen = (int)v + 4;
    if (hlit > 286 || hdist > 32)
        return 0;

    memset(code_lengths, 0, sizeof(code_lengths));
    memset(lengths, 0, sizeof(lengths));
    for (i = 0; i < hclen; i++) {
        if (!br_read(br, 3, &v)) return 0;
        code_lengths[codelen_order[i]] = (uint8_t)v;
    }
    if (!huff_build(&code_table, code_lengths, 19))
        return 0;

    total = hlit + hdist;
    while (used < total) {
        int sym;
        int repeat;
        if (!huff_read_symbol(br, &code_table, &sym))
            return 0;
        if (sym <= 15) {
            lengths[used++] = (uint8_t)sym;
        } else if (sym == 16) {
            uint8_t prev;
            if (used == 0)
                return 0;
            if (!br_read(br, 2, &v)) return 0;
            repeat = (int)v + 3;
            if (used + repeat > total)
                return 0;
            prev = lengths[used - 1];
            while (repeat--)
                lengths[used++] = prev;
        } else if (sym == 17) {
            if (!br_read(br, 3, &v)) return 0;
            repeat = (int)v + 3;
            if (used + repeat > total)
                return 0;
            used += repeat;
        } else if (sym == 18) {
            if (!br_read(br, 7, &v)) return 0;
            repeat = (int)v + 11;
            if (used + repeat > total)
                return 0;
            used += repeat;
        } else {
            return 0;
        }
    }

    return huff_build(lit, lengths, hlit) &&
           huff_build(dist, lengths + hlit, hdist);
}

static int decode_codes(bit_reader_t *br, uint8_t *dst, size_t dst_size,
                        size_t *out_pos, const huff_t *lit, const huff_t *dist) {
    for (;;) {
        int sym;
        if (!huff_read_symbol(br, lit, &sym))
            return 0;
        if (sym < 256) {
            if (*out_pos >= dst_size)
                return 0;
            dst[(*out_pos)++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 1;
        } else if (sym >= 257 && sym <= 285) {
            int len_idx = sym - 257;
            uint32_t extra;
            int length = len_base[len_idx];
            int dist_sym;
            int distance;
            int i;

            if (!br_read(br, (unsigned)len_extra[len_idx], &extra))
                return 0;
            length += (int)extra;

            if (!huff_read_symbol(br, dist, &dist_sym))
                return 0;
            if (dist_sym < 0 || dist_sym >= 30)
                return 0;
            if (!br_read(br, (unsigned)dist_extra[dist_sym], &extra))
                return 0;
            distance = dist_base[dist_sym] + (int)extra;
            if (distance <= 0 || (size_t)distance > *out_pos)
                return 0;
            if (*out_pos + (size_t)length > dst_size)
                return 0;
            for (i = 0; i < length; i++) {
                dst[*out_pos] = dst[*out_pos - (size_t)distance];
                (*out_pos)++;
            }
        } else {
            return 0;
        }
    }
}

int deflate_decode_raw(const uint8_t *src, size_t src_size,
                          uint8_t *dst, size_t dst_size,
                          size_t *actual_out) {
    bit_reader_t br;
    size_t out_pos = 0;
    int final = 0;

    br_init(&br, src, src_size);
    while (!final) {
        uint32_t v;
        int block_type;

        if (!br_read(&br, 1, &v)) return 0;
        final = (int)v;
        if (!br_read(&br, 2, &v)) return 0;
        block_type = (int)v;

        if (block_type == 0) {
            uint32_t len, nlen;
            size_t i;
            br_align_byte(&br);
            if (!br_read(&br, 16, &len)) return 0;
            if (!br_read(&br, 16, &nlen)) return 0;
            if (((len ^ 0xFFFFu) & 0xFFFFu) != nlen)
                return 0;
            if (out_pos + len > dst_size)
                return 0;
            for (i = 0; i < len; i++) {
                uint32_t byte;
                if (!br_read(&br, 8, &byte)) return 0;
                dst[out_pos++] = (uint8_t)byte;
            }
        } else if (block_type == 1 || block_type == 2) {
            huff_t lit;
            huff_t dist;
            if (block_type == 1) {
                if (!fixed_tables(&lit, &dist))
                    return 0;
            } else {
                if (!dynamic_tables(&br, &lit, &dist))
                    return 0;
            }
            if (!decode_codes(&br, dst, dst_size, &out_pos, &lit, &dist))
                return 0;
        } else {
            return 0;
        }
    }

    if (actual_out)
        *actual_out = out_pos;
    return 1;
}
