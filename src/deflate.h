#ifndef DEFLATE_H
#define DEFLATE_H

#include <stddef.h>
#include <stdint.h>

int deflate_decode_raw(const uint8_t *src, size_t src_size,
                          uint8_t *dst, size_t dst_size,
                          size_t *actual_out);

uint8_t *deflate_encode_raw(const uint8_t *data, size_t data_size,
                               size_t *out_size);

#endif /* DEFLATE_H */
