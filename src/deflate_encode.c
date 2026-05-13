#include "deflate.h"
#include "util.h"

#include <limits.h>

#define MIN_MATCH 3
#define MAX_MATCH 258
#define WINDOW_SIZE 32768
#define MATCH_LOOKAHEAD (MAX_MATCH + MIN_MATCH + 1)
#define MATCH_DISTANCE_LIMIT (WINDOW_SIZE - MATCH_LOOKAHEAD)
#define SHORT_MATCH_DISTANCE_LIMIT 4096
#define MATCH_HASH_BITS 15
#define MATCH_HASH_SIZE (1 << MATCH_HASH_BITS)
#define MATCH_HASH_MASK (MATCH_HASH_SIZE - 1)
#define MATCH_HASH_SHIFT ((MATCH_HASH_BITS + MIN_MATCH - 1) / MIN_MATCH)
#define FAST_CHAIN_MATCH 32
#define LAZY_MATCH_LIMIT 258
#define NO_POS (-1)
#define TOKEN_BLOCK_LIMIT 0x8000
#define MATCH_BLOCK_LIMIT 0x8000

static const int len_base_e[29] = {
    3, 4, 5, 6, 7, 8, 9, 10,
    11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115,
    131, 163, 195, 227, 258
};

static const int len_extra_e[29] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0
};

static const int dist_base_e[30] = {
    1, 2, 3, 4, 5, 7, 9, 13,
    17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073,
    4097, 6145, 8193, 12289, 16385, 24577
};

static const int dist_extra_e[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,
    3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13
};

static const int codelen_order_e[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static const uint8_t match_probe_tail[36] = {
    0x00,0x00,0x00,0x00, 0x03,0x00,0x00,0x00, 0xb5,0x2f,0x05,0x08,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x52,0xd0,0xff,0xff,
    0xd0,0x4a,0x05,0x08, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00
};

typedef struct {
    uint8_t type;
    uint16_t lit;
    uint16_t length;
    uint16_t distance;
    uint32_t end_pos;
} token_t;

typedef struct {
    token_t *data;
    size_t len;
    size_t cap;
} token_vec_t;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    uint64_t bitbuf;
    unsigned bitcount;
} bit_writer_t;

typedef struct {
    uint32_t code;
    uint8_t len;
} code_t;

typedef struct {
    int symbol;
    int extra;
    int bits;
} cl_item_t;

typedef struct {
    cl_item_t *data;
    size_t len;
    size_t cap;
} cl_vec_t;

static void token_push(token_vec_t *v, token_t t) {
    if (v->len >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 1024;
        v->data = (token_t *)realloc(v->data, v->cap * sizeof(token_t));
        if (!v->data) die("out of memory");
    }
    v->data[v->len++] = t;
}

static void cl_push(cl_vec_t *v, int symbol, int extra, int bits) {
    if (v->len >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 128;
        v->data = (cl_item_t *)realloc(v->data, v->cap * sizeof(cl_item_t));
        if (!v->data) die("out of memory");
    }
    v->data[v->len].symbol = symbol;
    v->data[v->len].extra = extra;
    v->data[v->len].bits = bits;
    v->len++;
}

static void bw_init(bit_writer_t *w) {
    w->cap = 1024;
    w->len = 0;
    w->data = (uint8_t *)malloc(w->cap);
    if (!w->data) die("out of memory");
    w->bitbuf = 0;
    w->bitcount = 0;
}

static void bw_push_byte(bit_writer_t *w, uint8_t b) {
    if (w->len >= w->cap) {
        w->cap *= 2;
        w->data = (uint8_t *)realloc(w->data, w->cap);
        if (!w->data) die("out of memory");
    }
    w->data[w->len++] = b;
}

static void bw_write(bit_writer_t *w, uint32_t value, unsigned bits) {
    if (bits == 32)
        w->bitbuf |= (uint64_t)value << w->bitcount;
    else
        w->bitbuf |= ((uint64_t)value & (((uint64_t)1 << bits) - 1)) << w->bitcount;
    w->bitcount += bits;
    while (w->bitcount >= 8) {
        bw_push_byte(w, (uint8_t)(w->bitbuf & 0xff));
        w->bitbuf >>= 8;
        w->bitcount -= 8;
    }
}

static uint8_t *bw_finish(bit_writer_t *w, size_t *out_size) {
    if (w->bitcount) {
        bw_push_byte(w, (uint8_t)(w->bitbuf & 0xff));
        w->bitbuf = 0;
        w->bitcount = 0;
    }
    *out_size = w->len;
    return w->data;
}

static uint32_t rev_bits(uint32_t value, int count) {
    uint32_t out = 0;
    int i;
    for (i = 0; i < count; i++) {
        out = (out << 1) | (value & 1u);
        value >>= 1;
    }
    return out;
}

static void canonical_codes(const uint8_t *lengths, int n, code_t *codes) {
    int bl_count[16];
    int next_code[16];
    int code = 0;
    int bits, symbol;
    memset(bl_count, 0, sizeof(bl_count));
    memset(next_code, 0, sizeof(next_code));
    for (symbol = 0; symbol < n; symbol++) {
        codes[symbol].code = 0;
        codes[symbol].len = lengths[symbol];
        if (lengths[symbol])
            bl_count[lengths[symbol]]++;
    }
    for (bits = 1; bits <= 15; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }
    for (symbol = 0; symbol < n; symbol++) {
        int len = lengths[symbol];
        if (len) {
            codes[symbol].code = rev_bits((uint32_t)next_code[len], len);
            next_code[len]++;
        }
    }
}

static void write_symbol(bit_writer_t *w, const code_t *codes, int symbol) {
    bw_write(w, codes[symbol].code, codes[symbol].len);
}

static int hash3(const uint8_t *data, size_t size, int pos) {
    unsigned h;
    if ((size_t)pos + 3 > size) return NO_POS;
    h = (((unsigned)data[pos] << MATCH_HASH_SHIFT) ^ data[pos + 1]) & MATCH_HASH_MASK;
    return (int)(((h << MATCH_HASH_SHIFT) ^ data[pos + 2]) & MATCH_HASH_MASK);
}

static void make_tail_window(const uint8_t *data, size_t size, uint8_t **out_win, int *out_base) {
    size_t cap = WINDOW_SIZE * 2 + sizeof(match_probe_tail) + 0x4000;
    uint8_t *window = (uint8_t *)calloc(cap, 1);
    size_t read_pos = 0;
    int base = 0;
    int local_pos = 0;
    if (!window) die("out of memory");

    while (read_pos < size) {
        size_t available, chunk;
        if (local_pos >= WINDOW_SIZE + MATCH_DISTANCE_LIMIT) {
            memmove(window, window + WINDOW_SIZE, WINDOW_SIZE);
            base += WINDOW_SIZE;
            local_pos -= WINDOW_SIZE;
        }
        available = WINDOW_SIZE * 2 - (size_t)local_pos;
        chunk = size - read_pos;
        if (chunk > available) chunk = available;
        memcpy(window + local_pos, data + read_pos, chunk);
        read_pos += chunk;
        local_pos += (int)chunk;
    }
    if (local_pos >= WINDOW_SIZE * 2 - 1) {
        memmove(window, window + WINDOW_SIZE, WINDOW_SIZE);
        base += WINDOW_SIZE;
    }
    memcpy(window + WINDOW_SIZE * 2, match_probe_tail, sizeof(match_probe_tail));
    *out_win = window;
    *out_base = base;
}

static uint8_t window_byte(const uint8_t *data, size_t size, int pos, const uint8_t *tail, int tail_base) {
    if (pos >= 0 && (size_t)pos < size)
        return data[pos];
    if (tail) {
        int local = pos - tail_base;
        if (local >= 0 && local < WINDOW_SIZE * 2 + (int)sizeof(match_probe_tail) + 0x4000)
            return tail[local];
    }
    return 0;
}

static int insert_string(const uint8_t *data, size_t size, int pos, int *head, int *prev) {
    int h, old;
    if ((size_t)pos + MIN_MATCH > size)
        return NO_POS;
    h = hash3(data, size, pos);
    old = head[h];
    prev[pos & (WINDOW_SIZE - 1)] = old;
    head[h] = pos;
    return old;
}

static void find_longest_match(const uint8_t *data, size_t size, int pos, int chain_head,
                          int *prev, int search_budget, int seed_length, int seed_start,
                          const uint8_t *tail, int tail_base, int *out_len, int *out_dist) {
    int cur, limit, include_limit, best_length, best_start, probe_end, chain_left;
    int eof_equal_updates = 0;
    if ((size_t)pos + MIN_MATCH > size) {
        *out_len = 0; *out_dist = 0; return;
    }
    cur = chain_head;
    limit = pos - MATCH_DISTANCE_LIMIT;
    if (limit < 1) limit = 1;
    include_limit = pos < WINDOW_SIZE + MATCH_DISTANCE_LIMIT;
    best_length = seed_length;
    best_start = seed_start;
    probe_end = pos + MAX_MATCH;
    chain_left = (seed_length >= FAST_CHAIN_MATCH) ? (search_budget >> 2) : search_budget;

    while ((include_limit ? (cur >= limit) : (cur > limit)) && chain_left && cur != NO_POS) {
        int n, max_n;
        chain_left--;
        if (window_byte(data, size, cur + best_length, tail, tail_base) !=
            window_byte(data, size, pos + best_length, tail, tail_base) ||
            data[cur] != data[pos]) {
            cur = prev[cur & (WINDOW_SIZE - 1)];
            continue;
        }
        n = 1;
        max_n = probe_end - pos;
        while (n < max_n &&
               window_byte(data, size, cur + n, tail, tail_base) ==
               window_byte(data, size, pos + n, tail, tail_base))
            n++;
        if (n > best_length) {
            best_length = n;
            best_start = cur;
            eof_equal_updates = 0;
            if (n >= MAX_MATCH)
                break;
        } else if (n == best_length && best_start != NO_POS &&
                   best_length > (int)(size - (size_t)pos) && eof_equal_updates < 4) {
            best_start = cur;
            eof_equal_updates++;
        }
        cur = prev[cur & (WINDOW_SIZE - 1)];
    }

    if (best_length < MIN_MATCH || best_start == NO_POS) {
        *out_len = 0; *out_dist = 0; return;
    }
    *out_len = best_length;
    *out_dist = pos - best_start;
    if (*out_len == MIN_MATCH && *out_dist > SHORT_MATCH_DISTANCE_LIMIT) {
        *out_len = 0; *out_dist = 0;
    }
}

static void find_match(const uint8_t *data, size_t size, int pos, int chain_head,
                       int *prev, int prev_len, int seed_start,
                       const uint8_t *tail, int tail_base, int *out_len, int *out_dist) {
    int remaining;
    find_longest_match(data, size, pos, chain_head, prev, 4096, prev_len, seed_start,
                  tail, tail_base, out_len, out_dist);
    remaining = (int)(size - (size_t)pos);
    if (*out_len > remaining) *out_len = remaining;
    if (*out_len == MIN_MATCH && *out_dist > SHORT_MATCH_DISTANCE_LIMIT) (*out_len)--;
}

static void tokenize(const uint8_t *data, size_t size, token_vec_t *tokens) {
    int *head = (int *)malloc(MATCH_HASH_SIZE * sizeof(int));
    int *prev = (int *)malloc(WINDOW_SIZE * sizeof(int));
    uint8_t *tail = NULL;
    int tail_base = 0;
    int pos = 0;
    int pending_literal = 0;
    int match_len = MIN_MATCH - 1;
    int match_start = NO_POS;
    uint32_t out_pos = 0;
    int i;
    if (!head || !prev) die("out of memory");
    for (i = 0; i < MATCH_HASH_SIZE; i++) head[i] = NO_POS;
    for (i = 0; i < WINDOW_SIZE; i++) prev[i] = NO_POS;
    make_tail_window(data, size, &tail, &tail_base);

    while ((size_t)pos < size) {
        int chain_head = ((size_t)pos + MIN_MATCH <= size) ? insert_string(data, size, pos, head, prev) : NO_POS;
        int prev_len = match_len;
        int seed_start = match_start;
        int match_dist = 0;
        match_len = MIN_MATCH - 1;
        match_start = NO_POS;

        if (chain_head != NO_POS && prev_len < LAZY_MATCH_LIMIT && pos - chain_head <= MATCH_DISTANCE_LIMIT) {
            find_match(data, size, pos, chain_head, prev, prev_len, seed_start,
                       tail, tail_base, &match_len, &match_dist);
            match_start = match_len >= MIN_MATCH ? pos - match_dist : NO_POS;
        }

        if (prev_len >= MIN_MATCH && seed_start != NO_POS && match_len <= prev_len) {
            token_t t;
            int end;
            t.type = 1;
            t.lit = 0;
            t.length = (uint16_t)prev_len;
            t.distance = (uint16_t)((pos - 1) - seed_start);
            out_pos += (uint32_t)prev_len;
            t.end_pos = out_pos;
            token_push(tokens, t);
            end = pos + prev_len - 1;
            if ((size_t)end > size) end = (int)size;
            pos++;
            while (pos < end) {
                if ((size_t)pos + MIN_MATCH <= size)
                    insert_string(data, size, pos, head, prev);
                pos++;
            }
            pending_literal = 0;
            match_len = MIN_MATCH - 1;
        } else if (pending_literal) {
            token_t t;
            t.type = 0;
            t.lit = data[pos - 1];
            t.length = 0;
            t.distance = 0;
            out_pos++;
            t.end_pos = out_pos;
            token_push(tokens, t);
            pos++;
        } else {
            pending_literal = 1;
            pos++;
        }
    }

    if (pending_literal && size) {
        token_t t;
        t.type = 0;
        t.lit = data[size - 1];
        t.length = 0;
        t.distance = 0;
        out_pos++;
        t.end_pos = out_pos;
        token_push(tokens, t);
    }

    free(tail);
    free(head);
    free(prev);
}

static int length_symbol(int length) {
    int i;
    if (length == 258) return 285;
    for (i = 0; i < 29; i++) {
        int extra = len_extra_e[i];
        if ((extra == 0 && length == len_base_e[i]) ||
            (extra != 0 && length >= len_base_e[i] && length < len_base_e[i] + (1 << extra)))
            return 257 + i;
    }
    return -1;
}

static int distance_symbol(int distance) {
    int i;
    for (i = 0; i < 30; i++) {
        int extra = dist_extra_e[i];
        if ((extra == 0 && distance == dist_base_e[i]) ||
            (extra != 0 && distance >= dist_base_e[i] && distance < dist_base_e[i] + (1 << extra)))
            return i;
    }
    return -1;
}

static int smaller_node(const uint32_t *freq, const uint8_t *depth, int a, int b) {
    return freq[a] < freq[b] || (freq[a] == freq[b] && depth[a] <= depth[b]);
}

static void downheap(int *heap, int heap_len, int idx, const uint32_t *freq, const uint8_t *depth) {
    int value = heap[idx];
    while (idx <= heap_len / 2) {
        int child = idx * 2;
        if (child < heap_len && smaller_node(freq, depth, heap[child + 1], heap[child]))
            child++;
        if (smaller_node(freq, depth, value, heap[child]))
            break;
        heap[idx] = heap[child];
        idx = child;
    }
    heap[idx] = value;
}

static void huff_lengths(const uint32_t *freqs, int n, int max_bits, uint8_t *out) {
    int max_nodes = n * 2 + 1;
    uint32_t node_freq[600];
    uint8_t depth[600];
    int parent[600];
    int heap[600];
    int heap_order[600];
    int active[300];
    int active_count = 0;
    int highest_symbol = -1;
    int heap_len, heap_max, next_node;
    int i, h, overflow = 0;
    uint8_t node_len[600];
    int bl_count[16];
    memset(out, 0, (size_t)n);
    memset(node_freq, 0, sizeof(node_freq));
    memset(depth, 0, sizeof(depth));
    for (i = 0; i < max_nodes; i++) parent[i] = -1;
    for (i = 0; i < n; i++) {
        node_freq[i] = freqs[i];
        if (freqs[i]) {
            active[active_count++] = i;
            highest_symbol = i;
        }
    }
    if (active_count == 0) return;
    heap[0] = 0;
    heap_len = 0;
    for (i = 0; i < active_count; i++) heap[++heap_len] = active[i];
    while (heap_len < 2) {
        int dummy = highest_symbol < 2 ? highest_symbol + 1 : 0;
        int exists = 1;
        while (exists) {
            int j;
            exists = 0;
            for (j = 0; j < active_count; j++) if (active[j] == dummy) exists = 1;
            if (exists) dummy++;
        }
        active[active_count++] = dummy;
        if (dummy > highest_symbol) highest_symbol = dummy;
        node_freq[dummy] = 1;
        heap[++heap_len] = dummy;
    }
    for (i = heap_len / 2; i >= 1; i--) downheap(heap, heap_len, i, node_freq, depth);
    heap_max = max_nodes;
    next_node = n;
    while (heap_len >= 2) {
        int n1, n2, node;
        n1 = heap[1];
        heap[1] = heap[heap_len--];
        downheap(heap, heap_len, 1, node_freq, depth);
        heap_order[--heap_max] = n1;
        n2 = heap[1];
        heap_order[--heap_max] = n2;
        node = next_node++;
        node_freq[node] = node_freq[n1] + node_freq[n2];
        depth[node] = (depth[n1] > depth[n2] ? depth[n1] : depth[n2]) + 1;
        parent[n1] = node;
        parent[n2] = node;
        heap[1] = node;
        downheap(heap, heap_len, 1, node_freq, depth);
    }
    heap_order[--heap_max] = heap[1];
    memset(node_len, 0, sizeof(node_len));
    memset(bl_count, 0, sizeof(bl_count));
    for (h = heap_max + 1; h < max_nodes; h++) {
        int node = heap_order[h];
        int bits = parent[node] >= 0 ? node_len[parent[node]] + 1 : 0;
        if (bits > max_bits) {
            bits = max_bits;
            overflow++;
        }
        node_len[node] = (uint8_t)bits;
        if (node <= highest_symbol) bl_count[bits]++;
    }
    if (overflow == 0) {
        for (i = 0; i < n && i <= highest_symbol; i++) out[i] = node_len[i];
        return;
    }
    while (overflow > 0) {
        int bits = max_bits - 1;
        while (bits > 0 && bl_count[bits] == 0) bits--;
        bl_count[bits]--;
        bl_count[bits + 1] += 2;
        bl_count[max_bits]--;
        overflow -= 2;
    }
    h = max_nodes;
    for (i = max_bits; i > 0; i--) {
        int count = bl_count[i];
        while (count) {
            int node = heap_order[--h];
            if (node > highest_symbol) continue;
            out[node] = (uint8_t)i;
            count--;
        }
    }
}

static void freqs_for_block(const token_t *tokens, size_t start, size_t end,
                            uint32_t *lit_freq, uint32_t *dist_freq) {
    size_t i;
    memset(lit_freq, 0, 286 * sizeof(uint32_t));
    memset(dist_freq, 0, 30 * sizeof(uint32_t));
    for (i = start; i < end; i++) {
        if (tokens[i].type == 0) {
            lit_freq[tokens[i].lit]++;
        } else {
            int lc = length_symbol(tokens[i].length);
            int dc = distance_symbol(tokens[i].distance);
            lit_freq[lc]++;
            dist_freq[dc]++;
        }
    }
    lit_freq[256]++;
    for (i = 0; i < 30; i++) if (dist_freq[i]) return;
    dist_freq[0] = 1;
}

static void trim_lengths(uint8_t *lit, int *lit_n, uint8_t *dist, int *dist_n) {
    int i, last_lit = 256, last_dist = 0;
    for (i = 0; i < 286; i++) if (lit[i]) last_lit = i;
    for (i = 0; i < 30; i++) if (dist[i]) last_dist = i;
    *lit_n = last_lit + 1;
    if (*lit_n < 257) *lit_n = 257;
    *dist_n = last_dist + 1;
    if (*dist_n < 1) *dist_n = 1;
}

static void encode_code_lengths(const uint8_t *lengths, int n, cl_vec_t *out) {
    int prev_len = -1;
    int next_len = n ? lengths[0] : 0xffff;
    int count = 0;
    int repeat_max = 7, repeat_min = 4;
    int i;
    if (next_len == 0) { repeat_max = 138; repeat_min = 3; }
    for (i = 0; i < n; i++) {
        int cur_len = next_len;
        next_len = (i + 1 < n) ? lengths[i + 1] : 0xffff;
        count++;
        if (count < repeat_max && cur_len == next_len) continue;
        if (count < repeat_min) {
            int j;
            for (j = 0; j < count; j++) cl_push(out, cur_len, 0, 0);
        } else if (cur_len != 0) {
            if (cur_len != prev_len) {
                cl_push(out, cur_len, 0, 0);
                count--;
            }
            cl_push(out, 16, count - 3, 2);
        } else if (count <= 10) {
            cl_push(out, 17, count - 3, 3);
        } else {
            cl_push(out, 18, count - 11, 7);
        }
        count = 0;
        prev_len = cur_len;
        if (next_len == 0) { repeat_max = 138; repeat_min = 3; }
        else if (cur_len == next_len) { repeat_max = 6; repeat_min = 3; }
        else { repeat_max = 7; repeat_min = 4; }
    }
}

static void fixed_lengths(uint8_t *lit, uint8_t *dist) {
    int i;
    memset(lit, 0, 288);
    for (i = 0; i < 144; i++) lit[i] = 8;
    for (i = 144; i < 256; i++) lit[i] = 9;
    for (i = 256; i < 280; i++) lit[i] = 7;
    for (i = 280; i < 288; i++) lit[i] = 8;
    for (i = 0; i < 32; i++) dist[i] = 5;
}

static void block_tables(const token_t *tokens, size_t start, size_t end,
                         uint8_t *lit_len, int *lit_n, uint8_t *dist_len, int *dist_n,
                         uint8_t *cl_len, cl_vec_t *cl_symbols, int *hclen) {
    uint32_t lit_freq[286], dist_freq[30], cl_freq[19];
    int i;
    freqs_for_block(tokens, start, end, lit_freq, dist_freq);
    huff_lengths(lit_freq, 286, 15, lit_len);
    huff_lengths(dist_freq, 30, 15, dist_len);
    trim_lengths(lit_len, lit_n, dist_len, dist_n);
    cl_symbols->data = NULL; cl_symbols->len = cl_symbols->cap = 0;
    encode_code_lengths(lit_len, *lit_n, cl_symbols);
    encode_code_lengths(dist_len, *dist_n, cl_symbols);
    memset(cl_freq, 0, sizeof(cl_freq));
    for (i = 0; (size_t)i < cl_symbols->len; i++) cl_freq[cl_symbols->data[i].symbol]++;
    huff_lengths(cl_freq, 19, 7, cl_len);
    *hclen = 4;
    for (i = 0; i < 19; i++) if (cl_len[codelen_order_e[i]]) *hclen = i + 1;
}

static uint32_t token_bits(const token_t *tokens, size_t start, size_t end,
                           const uint8_t *lit_len, const uint8_t *dist_len) {
    size_t i;
    uint32_t bits = 0;
    for (i = start; i < end; i++) {
        if (tokens[i].type == 0) bits += lit_len[tokens[i].lit];
        else {
            int lc = length_symbol(tokens[i].length);
            int dc = distance_symbol(tokens[i].distance);
            bits += lit_len[lc] + len_extra_e[lc - 257] + dist_len[dc] + dist_extra_e[dc];
        }
    }
    return bits + lit_len[256];
}

static int choose_kind(const token_t *tokens, size_t start, size_t end) {
    uint8_t lit_len[288], dist_len[32], cl_len[19], fixed_lit[288], fixed_dist[32];
    int lit_n, dist_n, hclen;
    cl_vec_t cl;
    uint32_t dyn_bits, fix_bits;
    size_t i;
    block_tables(tokens, start, end, lit_len, &lit_n, dist_len, &dist_n, cl_len, &cl, &hclen);
    dyn_bits = 5 + 5 + 4 + 3 * (uint32_t)hclen;
    for (i = 0; i < cl.len; i++) dyn_bits += cl_len[cl.data[i].symbol] + cl.data[i].bits;
    dyn_bits += token_bits(tokens, start, end, lit_len, dist_len);
    free(cl.data);
    fixed_lengths(fixed_lit, fixed_dist);
    fix_bits = token_bits(tokens, start, end, fixed_lit, fixed_dist);
    return (((fix_bits + 3 + 7) >> 3) <= ((dyn_bits + 3 + 7) >> 3)) ? 1 : 2;
}

static int should_split_block(const token_t *tokens, size_t start, size_t end, uint32_t start_out, uint32_t end_out) {
    size_t i, block_tokens = end - start;
    size_t matches = 0;
    uint32_t dist_freq[30];
    uint32_t out_bits;
    memset(dist_freq, 0, sizeof(dist_freq));
    for (i = start; i < end; i++) {
        if (tokens[i].type == 1) {
            matches++;
            dist_freq[distance_symbol(tokens[i].distance)]++;
        }
    }
    if (matches >= block_tokens / 2) return 0;
    out_bits = (uint32_t)block_tokens * 8;
    for (i = 0; i < 30; i++) out_bits += dist_freq[i] * (uint32_t)(5 + dist_extra_e[i]);
    return (out_bits >> 3) < (end_out - start_out) / 2;
}

static void write_token_data(bit_writer_t *w, const token_t *tokens, size_t start, size_t end,
                             const code_t *lit_codes, const code_t *dist_codes) {
    size_t i;
    for (i = start; i < end; i++) {
        if (tokens[i].type == 0) write_symbol(w, lit_codes, tokens[i].lit);
        else {
            int lc = length_symbol(tokens[i].length);
            int dc = distance_symbol(tokens[i].distance);
            write_symbol(w, lit_codes, lc);
            if (len_extra_e[lc - 257]) bw_write(w, tokens[i].length - len_base_e[lc - 257], len_extra_e[lc - 257]);
            write_symbol(w, dist_codes, dc);
            if (dist_extra_e[dc]) bw_write(w, tokens[i].distance - dist_base_e[dc], dist_extra_e[dc]);
        }
    }
    write_symbol(w, lit_codes, 256);
}

static void write_fixed_block(bit_writer_t *w, const token_t *tokens, size_t start, size_t end, int final) {
    uint8_t lit_len[288], dist_len[32];
    code_t lit_codes[288], dist_codes[32];
    fixed_lengths(lit_len, dist_len);
    canonical_codes(lit_len, 288, lit_codes);
    canonical_codes(dist_len, 32, dist_codes);
    bw_write(w, final ? 1 : 0, 1);
    bw_write(w, 1, 2);
    write_token_data(w, tokens, start, end, lit_codes, dist_codes);
}

static void write_dynamic_block(bit_writer_t *w, const token_t *tokens, size_t start, size_t end, int final) {
    uint8_t lit_len[288], dist_len[32], cl_len[19];
    code_t lit_codes[288], dist_codes[32], cl_codes[19];
    int lit_n, dist_n, hclen;
    cl_vec_t cl;
    size_t i;
    block_tables(tokens, start, end, lit_len, &lit_n, dist_len, &dist_n, cl_len, &cl, &hclen);
    canonical_codes(lit_len, lit_n, lit_codes);
    canonical_codes(dist_len, dist_n, dist_codes);
    canonical_codes(cl_len, 19, cl_codes);
    bw_write(w, final ? 1 : 0, 1);
    bw_write(w, 2, 2);
    bw_write(w, (uint32_t)(lit_n - 257), 5);
    bw_write(w, (uint32_t)(dist_n - 1), 5);
    bw_write(w, (uint32_t)(hclen - 4), 4);
    for (i = 0; (int)i < hclen; i++) bw_write(w, cl_len[codelen_order_e[i]], 3);
    for (i = 0; i < cl.len; i++) {
        write_symbol(w, cl_codes, cl.data[i].symbol);
        if (cl.data[i].bits) bw_write(w, (uint32_t)cl.data[i].extra, (unsigned)cl.data[i].bits);
    }
    write_token_data(w, tokens, start, end, lit_codes, dist_codes);
    free(cl.data);
}

uint8_t *deflate_encode_raw(const uint8_t *data, size_t data_size, size_t *out_size) {
    token_vec_t tokens;
    bit_writer_t w;
    size_t block_first = 0;
    uint32_t block_first_out = 0;
    size_t i;
    size_t block_tokens = 0, block_matches = 0;
    tokens.data = NULL; tokens.len = tokens.cap = 0;
    tokenize(data, data_size, &tokens);
    bw_init(&w);
    for (i = 0; i < tokens.len; i++) {
        int flush = 0;
        block_tokens++;
        if (tokens.data[i].type == 1) block_matches++;
        if (block_tokens == TOKEN_BLOCK_LIMIT - 1 || block_matches == MATCH_BLOCK_LIMIT) flush = 1;
        else if (block_tokens % 4096 == 0)
            flush = should_split_block(tokens.data, block_first, i + 1, block_first_out, tokens.data[i].end_pos);
        if (flush && tokens.data[i].end_pos < data_size) {
            int kind = choose_kind(tokens.data, block_first, i + 1);
            if (kind == 1) write_fixed_block(&w, tokens.data, block_first, i + 1, 0);
            else write_dynamic_block(&w, tokens.data, block_first, i + 1, 0);
            block_first = i + 1;
            block_first_out = tokens.data[i].end_pos;
            block_tokens = 0;
            block_matches = 0;
        }
    }
    {
        int kind = choose_kind(tokens.data, block_first, tokens.len);
        if (kind == 1) write_fixed_block(&w, tokens.data, block_first, tokens.len, 1);
        else write_dynamic_block(&w, tokens.data, block_first, tokens.len, 1);
    }
    free(tokens.data);
    return bw_finish(&w, out_size);
}
