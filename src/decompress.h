#ifndef DECOMPRESS_H
#define DECOMPRESS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Decompress a compressed OoT ROM.
 * Detects the ROM version and uses its known dmadata offset.
 * Returns a newly allocated buffer with the decompressed ROM.
 * Sets *out_size to the decompressed size.
 */
uint8_t *do_decompress_rom(const uint8_t *comp, size_t comp_size, size_t *out_size);

#endif /* DECOMPRESS_H */