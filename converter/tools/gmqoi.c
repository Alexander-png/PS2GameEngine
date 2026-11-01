/* gmqoi: decode GameMaker's custom QOI variant. Own implementation from the public format description.
 * usage: gmqoi <width> <height> < body.bin > rgba.raw   (body = bytes AFTER the 12-byte "fioq" header)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int sx(int v, int bits) { int m = 1 << (bits - 1); return (v ^ m) - m; }

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    long w = atol(argv[1]), h = atol(argv[2]);
    size_t cap = 1 << 20, len = 0;
    uint8_t *in = malloc(cap);
    size_t n;
    while ((n = fread(in + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) { cap *= 2; in = realloc(in, cap); }
    }
    uint8_t *out = malloc((size_t)w * h * 4);
    uint8_t idx[64 * 4];
    memset(idx, 0, sizeof idx);
    int r = 0, g = 0, b = 0, a = 255, run = 0;
    size_t pos = 0;
    for (size_t p = 0; p < (size_t)w * h; p++) {
        if (run > 0) {
            run--;
        } else if (pos < len) {
            int b1 = in[pos++];
            if ((b1 & 0xC0) == 0x00) {
                int ip = (b1 & 0x3F) << 2;
                r = idx[ip]; g = idx[ip + 1]; b = idx[ip + 2]; a = idx[ip + 3];
            } else if ((b1 & 0xE0) == 0x40) {
                run = b1 & 0x1F;
            } else if ((b1 & 0xE0) == 0x60) {
                int b2 = in[pos++];
                run = (((b1 & 0x1F) << 8) | b2) + 32;
            } else if ((b1 & 0xC0) == 0x80) {
                r += sx((b1 >> 4) & 3, 2); g += sx((b1 >> 2) & 3, 2); b += sx(b1 & 3, 2);
            } else if ((b1 & 0xE0) == 0xC0) {
                int m = (b1 << 8) | in[pos++];
                r += sx((m >> 8) & 0x1F, 5); g += sx((m >> 4) & 0xF, 4); b += sx(m & 0xF, 4);
            } else if ((b1 & 0xF0) == 0xE0) {
                int b2 = in[pos++], b3 = in[pos++];
                int m = (b1 << 16) | (b2 << 8) | b3;
                r += sx((m >> 15) & 0x1F, 5); g += sx((m >> 10) & 0x1F, 5);
                b += sx((m >> 5) & 0x1F, 5); a += sx(m & 0x1F, 5);
            } else { /* 0xF0 */
                if (b1 & 8) r = in[pos++];
                if (b1 & 4) g = in[pos++];
                if (b1 & 2) b = in[pos++];
                if (b1 & 1) a = in[pos++];
            }
            r &= 255; g &= 255; b &= 255; a &= 255;
            int ip = ((r ^ g ^ b ^ a) & 63) << 2;
            idx[ip] = r; idx[ip + 1] = g; idx[ip + 2] = b; idx[ip + 3] = a;
        }
        out[p * 4] = r; out[p * 4 + 1] = g; out[p * 4 + 2] = b; out[p * 4 + 3] = a;
    }
    fwrite(out, 1, (size_t)w * h * 4, stdout);
    return 0;
}
