#ifndef N64CRC_H
#define N64CRC_H

#include <stdint.h>

/*
 * Recalculate and update the N64 ROM header CRC fields in-place.
 * Prints a warning to stderr if the CIC chip is unrecognized.
 */
void n64crc(uint8_t *rom);

#endif /* N64CRC_H */
