#include "yaz0.h"
#include "util.h"

/* --- Encoder internals --- */

static int enc_find(const uint8_t *data, size_t array_ofs, size_t needle_ofs,
                    int needle_len, int start_index, int source_length) {
    int limit = source_length - needle_len + 1;
    if (limit <= 0) return -1;

    uint8_t needle_first = data[needle_ofs];

    while (start_index < limit) {
        int index = -1;
        for (int r = start_index; r < limit; r++) {
            if (data[array_ofs + r] == needle_first) {
                index = r;
                break;
            }
        }
        if (index == -1) return -1;

        int found = 1;
        for (int i = 0; i < needle_len; i++) {
            if (data[array_ofs + index + i] != data[needle_ofs + i]) {
                found = 0;
                break;
            }
        }
        if (found) return index;

        start_index = index + 1;
    }
    return -1;
}

static void enc_search(const uint8_t *data, int pos, int sz, int cap,
                        int *out_hitp, int *out_hitl) {
    int mp = (pos > 0x1000) ? (pos - 0x1000) : 0;
    int ml = (cap < (sz - pos)) ? cap : (sz - pos);
    if (ml < 3) {
        *out_hitp = 0;
        *out_hitl = 0;
        return;
    }

    int hitp = 0;
    int hitl = 3;

    if (mp < pos) {
        int hl = enc_find(data, mp, pos, hitl, 0, pos + hitl - mp);
        while (hl >= 0 && hl < (pos - mp)) {
            while (hitl < ml && data[pos + hitl] == data[mp + hl + hitl])
                hitl++;
            mp += hl;
            hitp = mp;
            if (hitl == ml) {
                *out_hitp = hitp;
                *out_hitl = hitl;
                return;
            }
            mp += 1;
            hitl += 1;
            if (mp >= pos) break;
            hl = enc_find(data, mp, pos, hitl, 0, pos + hitl - mp);
        }
    }

    if (hitl < 4) hitl = 1;
    *out_hitp = hitp;
    *out_hitl = hitl - 1;
}

/* --- Public API --- */

uint8_t *yaz0_encode(const uint8_t *data, size_t data_size, size_t *out_size) {
    if (data_size == 0) {
        uint8_t *hdr = (uint8_t *)calloc(16, 1);
        if (!hdr) die("out of memory");
        memcpy(hdr, "Yaz0", 4);
        *out_size = 16;
        return hdr;
    }

    int cap = 0x111;
    int sz = (int)data_size;
    int pos = 0;
    uint32_t flag = 0x80000000u;

    buf_t raws;
    u16arr_t ctrl;
    u32arr_t cmds;

    buf_init(&raws, data_size / 2);
    u16arr_init(&ctrl, data_size / 8);
    u32arr_init(&cmds, data_size / 32 + 1);

    u32arr_push(&cmds, 0);

    while (pos < sz) {
        int hitp, hitl;
        enc_search(data, pos, sz, cap, &hitp, &hitl);

        if (hitl < 3) {
            buf_push8(&raws, data[pos]);
            cmds.data[cmds.len - 1] |= flag;
            pos += 1;
        } else {
            int tstp, tstl;
            enc_search(data, pos + 1, sz, cap, &tstp, &tstl);
            if ((hitl + 1) < tstl) {
                buf_push8(&raws, data[pos]);
                cmds.data[cmds.len - 1] |= flag;
                pos += 1;
                flag >>= 1;
                if (flag == 0) {
                    flag = 0x80000000u;
                    u32arr_push(&cmds, 0);
                }
                hitl = tstl;
                hitp = tstp;
            }

            int e = pos - hitp - 1;
            pos += hitl;

            if (hitl < 0x12) {
                hitl -= 2;
                u16arr_push(&ctrl, (uint16_t)((hitl << 12) | e));
            } else {
                u16arr_push(&ctrl, (uint16_t)e);
                buf_push8(&raws, (uint8_t)(hitl - 0x12));
            }
        }

        flag >>= 1;
        if (flag == 0) {
            flag = 0x80000000u;
            u32arr_push(&cmds, 0);
        }
    }

    if (flag == 0x80000000u)
        cmds.len--;

    /* Build ctl byte array (4 bytes per cmd) */
    size_t ctl_byte_len = cmds.len * 4;
    uint8_t *ctl_bytes = (uint8_t *)malloc(ctl_byte_len);
    if (!ctl_bytes) die("out of memory");
    for (size_t i = 0; i < cmds.len; i++) {
        uint32_t cmd = cmds.data[i];
        ctl_bytes[i*4]   = (uint8_t)((cmd >> 24) & 0xFF);
        ctl_bytes[i*4+1] = (uint8_t)((cmd >> 16) & 0xFF);
        ctl_bytes[i*4+2] = (uint8_t)((cmd >> 8) & 0xFF);
        ctl_bytes[i*4+3] = (uint8_t)(cmd & 0xFF);
    }

    /* Serialize stream */
    size_t b_idx = 0, v_idx = 0, c_idx = 0;
    uint32_t bit = 0x10000;
    int dec_s = (int)data_size;

    buf_t out;
    buf_init(&out, data_size);

    while (dec_s > 0) {
        if (bit > 0xFFFF) {
            bit = ctl_bytes[b_idx++];
            buf_push8(&out, (uint8_t)(bit & 0xFF));
            bit |= 0x100;
        }
        if (bit & 0x80) {
            buf_push8(&out, raws.data[v_idx++]);
            dec_s -= 1;
        } else {
            uint16_t val = ctrl.data[c_idx++];
            buf_push8(&out, (uint8_t)((val >> 8) & 0xFF));
            buf_push8(&out, (uint8_t)(val & 0xFF));
            int length_nibble = (val >> 12) & 0xF;
            if (length_nibble == 0) {
                uint8_t extra = raws.data[v_idx++];
                buf_push8(&out, extra);
                length_nibble = extra + 16;
            }
            dec_s -= length_nibble + 2;
        }
        bit <<= 1;
    }

    /* 16-byte header + compressed stream */
    size_t total = 16 + out.len;
    uint8_t *result = (uint8_t *)malloc(total);
    if (!result) die("out of memory");
    memset(result, 0, 16);
    memcpy(result, "Yaz0", 4);
    put32(result, 4, (uint32_t)data_size);
    memcpy(result + 16, out.data, out.len);
    *out_size = total;

    free(ctl_bytes);
    buf_free(&raws);
    u16arr_free(&ctrl);
    u32arr_free(&cmds);
    buf_free(&out);

    return result;
}

uint32_t yaz0_decode(const uint8_t *src, size_t src_offset, size_t sz,
                     uint8_t *dst, size_t dst_offset) {
    if (sz < 16)
        die("invalid Yaz0 data: too short");
    if (memcmp(src + src_offset, "Yaz0", 4) != 0)
        die("invalid Yaz0 magic");

    uint32_t uncomp_size = get32(src, src_offset + 4);

    size_t sp = src_offset + 16; /* skip header */
    size_t dp = dst_offset;
    size_t end = dst_offset + uncomp_size;
    int valid_bit_count = 0;
    uint8_t curr_code_byte = 0;

    while (dp < end) {
        if (valid_bit_count == 0) {
            curr_code_byte = src[sp++];
            valid_bit_count = 8;
        }

        if (curr_code_byte & 0x80) {
            /* Literal byte */
            dst[dp++] = src[sp++];
        } else {
            /* Match (copy from earlier in output) */
            uint8_t byte1 = src[sp];
            uint8_t byte2 = src[sp + 1];
            sp += 2;

            uint32_t dist = ((byte1 & 0x0F) << 8) | byte2;
            size_t copy_src = dp - (dist + 1);

            int num_bytes = byte1 >> 4;
            if (num_bytes == 0) {
                num_bytes = src[sp++] + 0x12;
            } else {
                num_bytes += 2;
            }

            /* Copy one byte at a time (overlap copies are intentional in LZ77) */
            for (int j = 0; j < num_bytes; j++) {
                dst[dp++] = dst[copy_src++];
            }
        }

        valid_bit_count--;
        curr_code_byte <<= 1;
    }

    return uncomp_size;
}
