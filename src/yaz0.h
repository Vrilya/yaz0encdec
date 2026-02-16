#ifndef YAZ0_H
#define YAZ0_H

#include <stdint.h>
#include <stddef.h>

/*
 * Compress data into Yaz0 format.
 * Returns a newly allocated buffer containing the full Yaz0 stream
 * (16-byte header + compressed data). Sets *out_size to the total size.
 */
uint8_t *yaz0_encode(const uint8_t *data, size_t data_size, size_t *out_size);

/*
 * Decompress Yaz0 data from src[src_offset..] into dst[dst_offset..].
 * sz is the total compressed size including the 16-byte header.
 * Returns the uncompressed size.
 */
uint32_t yaz0_decode(const uint8_t *src, size_t src_offset, size_t sz,
                     uint8_t *dst, size_t dst_offset);

#endif /* YAZ0_H */
