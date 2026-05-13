#include "zlib.h"
#include "util.h"
#include "deflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZLIB_HEADER_SIZE    8   /* "ZLIB" + 4-byte big-endian uncompressed size */
#define IQUE_FOOTER_SIZE    8

static uint32_t crc32_le(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    for (i = 0; i < size; i++) {
        int bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int)(crc & 1u));
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put32le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

size_t zlib_decode(const uint8_t *src, size_t src_offset, size_t src_size,
                   uint8_t *dst, size_t dst_offset, size_t dst_size) {
    size_t actual_out = 0;
    if (!deflate_decode_raw(src + src_offset, src_size,
                               dst + dst_offset, dst_size,
                               &actual_out)) {
        fprintf(stderr, "zlib_decode: decompression error at offset 0x%zX\n",
                src_offset);
        return 0;
    }
    return actual_out;
}

uint8_t *zlib_encode(const uint8_t *data, size_t data_size,
                     size_t *out_size, int noheader) {
    size_t header_size   = noheader ? 0 : ZLIB_HEADER_SIZE;
    size_t footer_size   = noheader ? IQUE_FOOTER_SIZE : 0;
    size_t deflate_sz = 0;
    uint8_t *deflate = deflate_encode_raw(data, data_size, &deflate_sz);
    uint8_t *buf;
    if (!deflate || deflate_sz == 0) {
        free(deflate);
        fprintf(stderr, "zlib_encode: compression failed\n");
        return NULL;
    }

    buf = (uint8_t *)malloc(deflate_sz + header_size + footer_size);
    if (!buf) die("out of memory");

    if (!noheader) {
        buf[0] = 'Z'; buf[1] = 'L'; buf[2] = 'I'; buf[3] = 'B';
        buf[4] = (uint8_t)(data_size >> 24);
        buf[5] = (uint8_t)(data_size >> 16);
        buf[6] = (uint8_t)(data_size >>  8);
        buf[7] = (uint8_t)(data_size      );
    }
    memcpy(buf + header_size, deflate, deflate_sz);
    if (noheader) {
        uint8_t *footer = buf + header_size + deflate_sz;
        put32le(footer, crc32_le(data, data_size));
        put32le(footer + 4, (uint32_t)data_size);
    }
    free(deflate);

    *out_size = deflate_sz + header_size + footer_size;
    return buf;
}
