#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Fatal error - prints message and exits */
void die(const char *msg);

/* Big-endian 32-bit read/write */
uint32_t get32(const uint8_t *data, size_t offset);
void     put32(uint8_t *data, size_t offset, uint32_t value);

/* Alignment helpers */
size_t align16(size_t size);
size_t align8mb(size_t size);

/* Dynamic byte buffer */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} buf_t;

void buf_init(buf_t *b, size_t initial_cap);
void buf_push8(buf_t *b, uint8_t v);
void buf_free(buf_t *b);

/* Dynamic uint32_t array */
typedef struct {
    uint32_t *data;
    size_t    len;
    size_t    cap;
} u32arr_t;

void u32arr_init(u32arr_t *a, size_t initial_cap);
void u32arr_push(u32arr_t *a, uint32_t v);
void u32arr_free(u32arr_t *a);

/* Dynamic uint16_t array */
typedef struct {
    uint16_t *data;
    size_t    len;
    size_t    cap;
} u16arr_t;

void u16arr_init(u16arr_t *a, size_t initial_cap);
void u16arr_push(u16arr_t *a, uint16_t v);
void u16arr_free(u16arr_t *a);

#endif /* UTIL_H */