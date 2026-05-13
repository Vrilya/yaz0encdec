#ifndef ZLIB_H
#define ZLIB_H

#include <stdint.h>
#include <stddef.h>

/*
 * Decompress raw deflate data (no zlib/gzip framing).
 *
 *   src        - source buffer
 *   src_offset - byte offset within src where deflate data starts
 *   src_size   - number of bytes of deflate data
 *   dst        - destination buffer
 *   dst_offset - byte offset within dst to write decompressed data
 *   dst_size   - maximum decompressed bytes to write
 *
 * Returns the decompressed size, or 0 on error.
 */
size_t zlib_decode(const uint8_t *src, size_t src_offset, size_t src_size,
                   uint8_t *dst, size_t dst_offset, size_t dst_size);

/*
 * Compress data using raw deflate at the highest quality level.
 *
 *   data     - input data
 *   data_size - input size in bytes
 *   out_size  - receives the total output size
 *   noheader  - 1: raw deflate only (iQue style)
 *                0: 8-byte "ZLIB" header + uncompressed size + deflate
 *
 * Returns a newly allocated buffer, or NULL on error.
 */
uint8_t *zlib_encode(const uint8_t *data, size_t data_size,
                     size_t *out_size, int noheader);

#endif /* ZLIB_H */
