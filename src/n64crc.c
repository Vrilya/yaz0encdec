#include "n64crc.h"
#include "util.h"

#define N64_HEADER_SIZE  0x40
#define N64_BC_SIZE      (0x1000 - N64_HEADER_SIZE)
#define N64_CRC1_OFS     0x10
#define N64_CRC2_OFS     0x14
#define CHECKSUM_START   0x00001000
#define CHECKSUM_LENGTH  0x00100000
#define CHECKSUM_CIC6102 0xF8CA4DDCu
#define CHECKSUM_CIC6103 0xA3886759u
#define CHECKSUM_CIC6105 0xDF26F436u
#define CHECKSUM_CIC6106 0x1FEA617Au

static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void n64_gen_table(void) {
    if (crc_table_ready) return;
    uint32_t poly = 0xEDB88320u;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ poly;
            else         crc = crc >> 1;
        }
        crc_table[i] = crc;
    }
    crc_table_ready = 1;
}

static uint32_t n64_crc32(const uint8_t *data, size_t offset, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++)
        crc = (crc >> 8) ^ crc_table[(crc ^ data[offset + i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

static int n64_get_cic(const uint8_t *data) {
    uint32_t crc = n64_crc32(data, N64_HEADER_SIZE, N64_BC_SIZE);
    switch (crc) {
        case 0x6170A4A1u: return 6101;
        case 0x90BB6CB5u: return 6102;
        case 0x0B050EE0u: return 6103;
        case 0x98BC2C86u: return 6105;
        case 0xACC8580Au: return 6106;
        default: return 0;
    }
}

static uint32_t rol32(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

void n64crc(uint8_t *rom) {
    n64_gen_table();
    int bootcode = n64_get_cic(rom);
    if (bootcode == 0) {
        fprintf(stderr, "warning: unknown CIC chip, CRC not updated\n");
        return;
    }

    uint32_t seed;
    switch (bootcode) {
        case 6101: case 6102: seed = CHECKSUM_CIC6102; break;
        case 6103: seed = CHECKSUM_CIC6103; break;
        case 6105: seed = CHECKSUM_CIC6105; break;
        case 6106: seed = CHECKSUM_CIC6106; break;
        default: return;
    }

    uint32_t t1=seed, t2=seed, t3=seed, t4=seed, t5=seed, t6=seed;

    for (size_t i = CHECKSUM_START; i < CHECKSUM_START + CHECKSUM_LENGTH; i += 4) {
        uint32_t d = get32(rom, i);
        if ((t6 + d) < t6) t4++;
        t6 += d;
        t3 ^= d;
        uint32_t r = rol32(d, d & 0x1F);
        t5 += r;
        if (t2 > d) t2 ^= r;
        else        t2 ^= t6 ^ d;
        if (bootcode == 6105)
            t1 += get32(rom, N64_HEADER_SIZE + 0x0710 + (i & 0xFF)) ^ d;
        else
            t1 += t5 ^ d;
    }

    uint32_t crc0, crc1;
    if (bootcode == 6103) {
        crc0 = (t6 ^ t4) + t3;  crc1 = (t5 ^ t2) + t1;
    } else if (bootcode == 6106) {
        crc0 = (t6 * t4) + t3;  crc1 = (t5 * t2) + t1;
    } else {
        crc0 = t6 ^ t4 ^ t3;   crc1 = t5 ^ t2 ^ t1;
    }
    put32(rom, N64_CRC1_OFS, crc0);
    put32(rom, N64_CRC2_OFS, crc1);
}