#include "util.h"

void die(const char *msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

uint32_t get32(const uint8_t *data, size_t offset) {
    return ((uint32_t)data[offset] << 24) |
           ((uint32_t)data[offset+1] << 16) |
           ((uint32_t)data[offset+2] << 8) |
           (uint32_t)data[offset+3];
}

void put32(uint8_t *data, size_t offset, uint32_t value) {
    data[offset]   = (uint8_t)(value >> 24);
    data[offset+1] = (uint8_t)(value >> 16);
    data[offset+2] = (uint8_t)(value >> 8);
    data[offset+3] = (uint8_t)(value);
}

size_t align16(size_t size) {
    return (size + 15) & ~(size_t)15;
}

size_t align8mb(size_t size) {
    size_t a = 8 * 0x100000;
    return ((size + a - 1) / a) * a;
}

/* --- buf_t --- */

void buf_init(buf_t *b, size_t initial_cap) {
    if (initial_cap < 64) initial_cap = 64;
    b->data = (uint8_t *)malloc(initial_cap);
    if (!b->data) die("out of memory");
    b->len = 0;
    b->cap = initial_cap;
}

void buf_ensure(buf_t *b, size_t extra) {
    if (b->len + extra > b->cap) {
        while (b->len + extra > b->cap)
            b->cap *= 2;
        b->data = (uint8_t *)realloc(b->data, b->cap);
        if (!b->data) die("out of memory");
    }
}

void buf_push8(buf_t *b, uint8_t v) {
    buf_ensure(b, 1);
    b->data[b->len++] = v;
}

void buf_free(buf_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* --- u32arr_t --- */

void u32arr_init(u32arr_t *a, size_t initial_cap) {
    if (initial_cap < 16) initial_cap = 16;
    a->data = (uint32_t *)malloc(initial_cap * sizeof(uint32_t));
    if (!a->data) die("out of memory");
    a->len = 0;
    a->cap = initial_cap;
}

void u32arr_push(u32arr_t *a, uint32_t v) {
    if (a->len >= a->cap) {
        a->cap *= 2;
        a->data = (uint32_t *)realloc(a->data, a->cap * sizeof(uint32_t));
        if (!a->data) die("out of memory");
    }
    a->data[a->len++] = v;
}

void u32arr_free(u32arr_t *a) {
    free(a->data);
    a->data = NULL;
    a->len = a->cap = 0;
}

/* --- u16arr_t --- */

void u16arr_init(u16arr_t *a, size_t initial_cap) {
    if (initial_cap < 16) initial_cap = 16;
    a->data = (uint16_t *)malloc(initial_cap * sizeof(uint16_t));
    if (!a->data) die("out of memory");
    a->len = 0;
    a->cap = initial_cap;
}

void u16arr_push(u16arr_t *a, uint16_t v) {
    if (a->len >= a->cap) {
        a->cap *= 2;
        a->data = (uint16_t *)realloc(a->data, a->cap * sizeof(uint16_t));
        if (!a->data) die("out of memory");
    }
    a->data[a->len++] = v;
}

void u16arr_free(u16arr_t *a) {
    free(a->data);
    a->data = NULL;
    a->len = a->cap = 0;
}
