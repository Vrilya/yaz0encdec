#include "romdb.h"
#include "dma.h"

#include <string.h>

#define BUILD_DATE_LEN 17

/* --- Skip index lists --- */

static const int skip_ntsc10_swe[] = {
    0,1,2,3,4,5,6,7,8,9,15,16,17,18,19,20,21,22,23,24,25,26,
    501,607,624,648,649,737,841,856,869,
    942,944,946,948,950,952,954,956,958,960,962,964,966,968,970,
    972,974,976,978,980,982,984,986,988,990,992,994,996,998,1000,
    1002,1004,1005,1006,
    1497,1498,1499,1500,1501,1502,1503,1504,1505,1506,1507,1508,
    1510,1511,1512,1513,1514,1515,1516,1517,1518,1519,1520,1521,
    1522,1523,1524,1525, -1
};

static const int skip_ntsc11[] = {
    0,1,2,3,4,5,6,7,8,9,15,16,17,18,19,20,21,22,23,24,25,26,
    942,944,946,948,950,952,954,956,958,960,962,964,966,968,970,
    972,974,976,978,980,982,984,986,988,990,992,994,996,998,1000,
    1002,1004,
    1510,1511,1512,1513,1514,1515,1516,1517,1518,1519,1520,1521,
    1522,1523,1524,1525, -1
};

static const int skip_ntscgc[] = {
    0,1,2,3,4,5,6,7,8,9,15,16,17,18,19,20,21,22,23,24,25,26,
    941,943,945,947,949,951,953,955,957,959,961,963,965,967,969,
    971,973,975,977,979,981,983,985,987,989,991,993,995,997,999,
    1001,1003,
    1509,1510,1511,1512,1513,1514,1515,1516,1517,1518,1519,1520,
    1521,1522,1523,1524, -1
};

static const int skip_pal10_swe[] = {
    0,1,2,3,4,5,6,7,8,15,16,17,18,19,20,21,22,23,24,25,26,27,
    502,608,625,649,650,738,842,857,870,
    943,945,947,949,951,953,955,957,959,961,963,965,967,969,971,
    973,975,977,979,981,983,985,987,989,991,993,995,997,999,1001,
    1003,1005,1006,1007,
    1498,1499,1500,1501,1502,1503,1504,1505,1506,1507,1508,1509,
    1511,1512,1513,1514,1515,1516,1517,1518,1519,1520,1521,1522,
    1523,1524,1525,1526, -1
};

static const int skip_pal10_retail[] = {
    0,1,2,3,4,5,6,7,8,15,16,17,18,19,20,21,22,23,24,25,26,27,
    943,945,947,949,951,953,955,957,959,961,963,965,967,969,971,
    973,975,977,979,981,983,985,987,989,991,993,995,997,999,1001,
    1003,1005,
    1511,1512,1513,1514,1515,1516,1517,1518,1519,1520,1521,1522,
    1523,1524,1525,1526, -1
};

static const int skip_palgc[] = {
    0,1,2,3,4,5,6,7,8,15,16,17,18,19,20,21,22,23,24,25,26,27,
    942,944,946,948,950,952,954,956,958,960,962,964,966,968,970,
    972,974,976,978,980,982,984,986,988,990,992,994,996,998,1000,
    1002,1004,
    1510,1511,1512,1513,1514,1515,1516,1517,1518,1519,1520,1521,
    1522,1523,1524,1525, -1
};

static const int skip_mm_ntscu[] = {
    0,1,2,3,4,5,6,7,8,9,
    15,16,17,18,19,20,21,22,
    25,26,27,28,29,30,
    652,1127,
    1539,1540,1541,1542,1543,1544,1545,1546,1547,1548,
    1549,1550,1551,1552,1553,1554,1555,1556,1557,1558,
    1559,1560,1561,1562,1563,1564,1565,1566,1567, -1
};

/* --- Version database --- */

const rom_version_t rom_versions[] = {
    { "Retail NTSC 1.0",          "98-10-21 04:56:31", 0x740C, 0x7430, 1526, skip_ntsc11,       CODEC_YAZ0, 0,  0 },
    { "Retail NTSC 1.1",          "98-10-26 10:58:45", 0x740C, 0x7430, 1526, skip_ntsc11,       CODEC_YAZ0, 0,  0 },
    { "Retail NTSC 1.2",          "98-11-12 18:17:03", 0x793C, 0x7960, 1526, skip_ntsc11,       CODEC_YAZ0, 0,  0 },
    { "Retail PAL 1.0",           "98-11-10 14:34:22", 0x792C, 0x7950, 1527, skip_pal10_retail, CODEC_YAZ0, 0,  0 },
    { "Retail PAL 1.1",           "98-11-18 17:36:49", 0x792C, 0x7950, 1527, skip_pal10_retail, CODEC_YAZ0, 0,  0 },
    { "Retail NTSC Master Quest", "02-12-19 14:05:42", 0x7150, 0x7170, 1525, skip_ntscgc,       CODEC_YAZ0, 0,  0 },
    { "Retail NTSC GameCube",     "02-12-19 13:28:09", 0x7150, 0x7170, 1525, skip_ntscgc,       CODEC_YAZ0, 0,  0 },
    { "Retail PAL Master Quest",  "03-02-21 20:37:19", 0x7150, 0x7170, 1526, skip_palgc,        CODEC_YAZ0, 0,  0 },
    { "Retail PAL GameCube",      "03-02-21 20:12:23", 0x7150, 0x7170, 1526, skip_palgc,        CODEC_YAZ0, 0,  0 },
    { "SWE NTSC 1.0",             "26-05-18 10:00:04", 0x740C, 0x7430, 1526, skip_ntsc10_swe,   CODEC_YAZ0, 0,  0 },
    { "SWE NTSC 1.1",             "26-05-18 10:00:05", 0x740C, 0x7430, 1526, skip_ntsc11,       CODEC_YAZ0, 0,  0 },
    { "SWE NTSC 1.2",             "26-05-18 10:00:06", 0x793C, 0x7960, 1526, skip_ntsc10_swe,   CODEC_YAZ0, 0,  0 },
    { "SWE PAL 1.0",              "26-05-18 10:00:09", 0x792C, 0x7950, 1527, skip_pal10_swe,    CODEC_YAZ0, 0,  0 },
    { "SWE PAL 1.1",              "26-05-18 10:00:10", 0x794C, 0x7970, 1527, skip_pal10_swe,    CODEC_YAZ0, 0,  0 },
    { "SWE NTSC Master Quest",    "26-05-18 10:00:08", 0x7150, 0x7170, 1525, skip_ntscgc,       CODEC_YAZ0, 0,  0 },
    { "SWE NTSC GameCube",        "26-05-18 10:00:07", 0x71D0, 0x71F0, 1525, skip_ntscgc,       CODEC_YAZ0, 0,  0 },
    { "SWE PAL Master Quest",     "26-05-18 10:00:12", 0x71D0, 0x71F0, 1526, skip_palgc,        CODEC_YAZ0, 0,  0 },
    { "SWE PAL GameCube",         "26-05-18 10:00:11", 0x71D0, 0x71F0, 1526, skip_palgc,        CODEC_YAZ0, 0,  0 },
    { "SWE PAL OTR",              "98-11-10 11:11:11", 0x792C, 0x7950, 1527, skip_pal10_swe,    CODEC_YAZ0, 0,  0 },
    { "SWE NTSC iQue",            "26-05-18 10:00:02", 0xB75C, 0xB780, 1525, skip_ntscgc,       CODEC_ZLIB, 1, 29 },
    { "SWE PAL iQue",             "26-05-18 10:00:03", 0xB75C, 0xB780, 1525, skip_ntscgc,       CODEC_ZLIB, 1, 29 },
    { "SWE NTSC MQ iQue",         "26-05-18 10:00:00", 0xB75C, 0xB780, 1525, skip_ntscgc,       CODEC_ZLIB, 1, 29 },
    { "SWE PAL MQ iQue",          "26-05-18 10:00:01", 0xB75C, 0xB780, 1525, skip_ntscgc,       CODEC_ZLIB, 1, 29 },
    { "Normal iQue",              "03-10-22 16:23:19", 0xB77C, 0xB7A0, 1525, skip_ntscgc,       CODEC_ZLIB, 1,  0 },
    { "Majora's Mask NTSC-U",     "00-07-31 17:04:16", 0x1A4DC, 0x1A500, 1568, skip_mm_ntscu,   CODEC_YAZ0, 0, 32 },
};

const size_t num_rom_versions = sizeof(rom_versions) / sizeof(rom_versions[0]);

const rom_version_t *detect_rom_version(const uint8_t *rom_data, size_t rom_size) {
    for (size_t i = 0; i < num_rom_versions; i++) {
        const rom_version_t *v = &rom_versions[i];
        if (v->build_offset + BUILD_DATE_LEN > rom_size)
            continue;
        if (memcmp(rom_data + v->build_offset, v->build_date, BUILD_DATE_LEN) == 0)
            return v;
    }
    return NULL;
}

void apply_rom_config(const rom_version_t *ver) {
    for (int i = 0; i < num_entries; i++)
        entries[i].compress = 1;
    for (const int *p = ver->skip_indices; *p >= 0; p++)
        if (*p < num_entries)
            entries[*p].compress = 0;
}
