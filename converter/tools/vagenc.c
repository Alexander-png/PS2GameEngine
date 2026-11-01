/* vagenc: mono s16le PCM (stdin) -> SPU ADPCM blocks (stdout). Own implementation of the standard SPU-ADPCM scheme:
 * 16-byte block = [predictor<<4 | shift][flags][14 bytes = 28 4-bit samples, low nibble first];
 * decode: s = ((nibble<<12) >> shift) + (s1*f0 + s2*f1 >> 6); filters (f0,f1): (0,0)(60,0)(115,-52)(98,-55)(122,-60).
 * Encoder: closed loop, tries all 5 filters, shift from the residual range (and +-1), keeps the least squared error.
 * usage: vagenc [loop]    loop=1: first block flag 0x06, middle 0x02, last 0x03; default one-shot: 0x00 ... last 0x01 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const int F0[5] = {0, 60, 115, 98, 122};
static const int F1[5] = {0, 0, -52, -55, -60};

static int clamp16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

/* encode one block with predictor p, shift sh from state (s1,s2); out nibbles; returns squared error, updates state copy */
static double try_block(const int16_t *x, int p, int sh, int *s1, int *s2, uint8_t *nib)
{
    int a = *s1, b = *s2;
    double err = 0;
    int step = 1 << (12 - sh);
    for (int i = 0; i < 28; i++) {
        int pred = (a * F0[p] + b * F1[p]) >> 6;
        int r = x[i] - pred;
        int n = (r >= 0) ? (r + step / 2) / step : -((-r + step / 2) / step);
        if (n > 7) n = 7;
        if (n < -8) n = -8;
        int rec = clamp16(((n * (1 << 12)) >> sh) + pred);
        double d = x[i] - rec;
        err += d * d;
        nib[i] = (uint8_t)(n & 15);
        b = a;
        a = rec;
    }
    *s1 = a;
    *s2 = b;
    return err;
}

int main(int argc, char **argv)
{
    int loop = argc > 1 && atoi(argv[1]);
    size_t cap = 1 << 20, n = 0;
    int16_t *pcm = malloc(cap * 2);
    for (;;) {
        if (n == cap) { cap *= 2; pcm = realloc(pcm, cap * 2); }
        size_t r = fread(pcm + n, 2, cap - n, stdin);
        if (!r) break;
        n += r;
    }
    size_t blocks = (n + 27) / 28;
    if (!blocks) blocks = 1;
    uint8_t *outb = malloc(blocks * 16);
    int s1 = 0, s2 = 0;
    /* loop mode: the SPU decoder keeps its history (s1,s2) when it wraps from the last block to the first, so the first
     * block must be encoded from the *end* state. Iterate encode passes until the end state is stable. */
    int passes = loop ? 6 : 1;
    for (int pass = 0; pass < passes; pass++) {
        int e1 = s1, e2 = s2;
        for (size_t bi = 0; bi < blocks; bi++) {
            int16_t x[28];
            for (int i = 0; i < 28; i++) { size_t k = bi * 28 + i; x[i] = k < n ? pcm[k] : 0; }
            double best = 1e300; int bp = 0, bs = 12, b1 = 0, b2 = 0; uint8_t bn[28];
            for (int p = 0; p < 5; p++) {
                int mx = 0, a = e1, b = e2;
                for (int i = 0; i < 28; i++) {
                    int pred = (a * F0[p] + b * F1[p]) >> 6;
                    int r = x[i] - pred;
                    if (abs(r) > mx) mx = abs(r);
                    b = a; a = x[i];
                }
                int sh = 12;
                while (sh > 0 && ((mx + (1 << (11 - sh < 0 ? 0 : 11 - sh))) >> (12 - sh)) > 7) sh--;
                for (int d = -1; d <= 1; d++) {
                    int sx = sh + d;
                    if (sx < 0 || sx > 12) continue;
                    int t1 = e1, t2 = e2; uint8_t nb[28];
                    double e = try_block(x, p, sx, &t1, &t2, nb);
                    if (e < best) { best = e; bp = p; bs = sx; b1 = t1; b2 = t2; memcpy(bn, nb, 28); }
                }
            }
            uint8_t *out = outb + bi * 16;
            out[0] = (uint8_t)((bp << 4) | bs);
            int last = bi == blocks - 1;
            out[1] = loop ? (bi == 0 ? 0x06 : (last ? 0x03 : 0x02)) : (last ? 0x01 : 0x00);
            for (int i = 0; i < 14; i++) out[2 + i] = (uint8_t)(bn[2 * i] | (bn[2 * i + 1] << 4));
            e1 = b1; e2 = b2;
        }
        if (getenv("VAGDEBUG")) fprintf(stderr, "pass %d start (%d,%d) end (%d,%d)\n", pass, s1, s2, e1, e2);
        if (e1 == s1 && e2 == s2) break;
        s1 = e1; s2 = e2;
    }
    fwrite(outb, 1, blocks * 16, stdout);
    return 0;
}
