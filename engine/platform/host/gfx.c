/* Host software renderer implementing ge/gfx.h (nearest sampling, GM-style alpha blend, 2x scale like the PS2 module).
 * Used by the test harness to look at frames without an emulator: ge_gfx_host_dump(path) writes the last frame as PPM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ge/gfx.h"

#define MAXTEX 4096
typedef struct { ge_texdesc_t d; uint32_t *rgba; int w, h; uint32_t key; int used; } tex_t;
static tex_t T[MAXTEX]; static int nt;
static int SW, SH, VW = 320, VH = 240;
static float scale = 2.0f;
static uint32_t *fb;
static unsigned up, hits;

int ge_gfx_init(int w, int h) { SW = w; SH = h; fb = calloc((size_t)w * h, 4); VW = w / 2; VH = h / 2; scale = 2; return 0; }
void ge_gfx_set_tilt(float deg) { (void)deg; }   /* the host renderer draws untilted */
void ge_gfx_set_view(int vw, int vh) { if (vw > 0) { VW = vw; VH = vh; scale = (float)SW / vw; } }
void ge_gfx_get_view(int *vw, int *vh) { *vw = VW; *vh = VH; }
ge_tex_t ge_gfx_tex_create(const ge_texdesc_t *d)
{
    if (d->key) for (int i = 0; i < nt; i++) if (T[i].key == d->key) { hits++; return i; }
    if (nt >= MAXTEX) return -1;
    T[nt].d = *d; T[nt].key = d->key; T[nt].rgba = NULL; T[nt].w = d->w; T[nt].h = d->h;
    return nt++;
}
void ge_gfx_tex_release(ge_tex_t t) { (void)t; }
static int decode(tex_t *t)
{
    const void *clut, *pix;
    if (t->d.load) { if (t->d.load(t->d.user, &clut, &pix)) return -1; } else { clut = t->d.clut; pix = t->d.pixels; }
    int t4 = t->d.fmt == GE_FMT_I4, st = t->d.stride;
    t->rgba = malloc((size_t)t->w * t->h * 4);
    const uint8_t *c = clut, *p = pix;
    for (int y = 0; y < t->h; y++) for (int x = 0; x < t->w; x++) {
        int idx = t4 ? ((p[(y * st + x) / 2] >> (4 * (x & 1))) & 15) : p[y * st + x];
        if (!t4) idx = (idx & 0xE7) | ((idx & 0x08) << 1) | ((idx & 0x10) >> 1);        /* CLUT8 is stored in CSM1 order */
        uint32_t a = c[idx * 4 + 3];
        a = a >= 128 ? 255 : a * 2;
        t->rgba[y * t->w + x] = c[idx * 4] | (c[idx * 4 + 1] << 8) | (c[idx * 4 + 2] << 16) | (a << 24);
    }
    if (getenv("GD_DUMPTEX")) {
        char n[64]; snprintf(n, sizeof n, "%s/tex%d.ppm", getenv("GD_DUMPTEX"), (int)(t - T));
        FILE *f = fopen(n, "wb");
        if (f) { fprintf(f, "P6\n%d %d\n255\n", t->w, t->h); for (int i = 0; i < t->w * t->h; i++) { uint32_t v = t->rgba[i]; uint8_t a = v >> 24, p[3] = {(v & 255) * a / 255, ((v >> 8) & 255) * a / 255, ((v >> 16) & 255) * a / 255}; fwrite(p, 1, 3, f); } fclose(f); }
        fprintf(stderr, "tex%d fmt %d %dx%d stride %d\n", (int)(t - T), t->d.fmt, t->w, t->h, st);
    }
    up++;
    return 0;
}
void ge_gfx_tex_prefetch(ge_tex_t t) { if (t >= 0 && t < nt && !T[t].rgba) decode(&T[t]); }
void ge_gfx_frame_begin(uint32_t bg)
{
    uint32_t c = 0xFF000000u | (bg & 0xFFFFFF);
    for (size_t i = 0; i < (size_t)SW * SH; i++) fb[i] = c;
}
static void blend(int x, int y, uint32_t src, uint32_t mod)
{
    if (x < 0 || y < 0 || x >= SW || y >= SH) return;
    unsigned sa = (src >> 24) * (mod >> 24) / 255;
    if (!sa) return;
    unsigned r = (src & 255) * (mod & 255) / 255, g = ((src >> 8) & 255) * ((mod >> 8) & 255) / 255, b = ((src >> 16) & 255) * ((mod >> 16) & 255) / 255;
    uint32_t d = fb[y * SW + x];
    unsigned dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
    dr += ((int)r - (int)dr) * (int)sa / 255; dg += ((int)g - (int)dg) * (int)sa / 255; db += ((int)b - (int)db) * (int)sa / 255;
    fb[y * SW + x] = 0xFF000000u | dr | (dg << 8) | (db << 16);
}
static int st_flat, st_calls; static double st_area;
void ge_gfx_draw_xf(ge_tex_t t, int sx, int sy, int w, int h, float ax, float ay, float rx, float ry, float scx, float scy, float rot, uint32_t color)
{
    if (t < 0 || t >= nt || scx == 0 || scy == 0 || w <= 0 || h <= 0) return;
    tex_t *tx = &T[t];
    if (!tx->rgba && decode(tx)) return;
    if (getenv("GT_TEXDBG")) { static int seen[600]; if (t < 600 && !seen[t]) { seen[t] = 1; fprintf(stderr, "tex %d key %u fmt %d %dx%d stride %d pix %u clut %u\n", t, tx->d.key, tx->d.fmt, tx->d.w, tx->d.h, tx->d.stride, (unsigned)tx->d.pixels_len, (unsigned)tx->d.clut_len); } }
    if (getenv("GT_DRAWDBG")) fprintf(stderr, "draw tex %u src %d,%d %dx%d at %g,%g sc %g,%g rot %g\n", tx->d.key, sx, sy, w, h, ax + rx, ay + ry, scx, scy, rot);
    st_calls++; st_area += (double)w*h*fabsf(scx*scy);
    if (getenv("GT_BIG") && (double)w*h*fabsf(scx*scy) > 20000) fprintf(stderr, "big %dx%d sc %g,%g col %08x\n", w, h, scx, scy, color);
    float a = rot * 0.017453292519943295f, ca = cosf(a), sa = sinf(a);
    float px[4], py[4], minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < 4; i++) {
        float dx = (rx + (i & 1) * w) * scx, dy = (ry + (i >> 1) * h) * scy;
        px[i] = (ax + dx * ca + dy * sa) * scale; py[i] = (ay - dx * sa + dy * ca) * scale;
        if (px[i] < minx) minx = px[i]; if (px[i] > maxx) maxx = px[i]; if (py[i] < miny) miny = py[i]; if (py[i] > maxy) maxy = py[i];
    }
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SW) x1 = SW; if (y1 > SH) y1 = SH;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float ox = (x + 0.5f) / scale - ax, oy = (y + 0.5f) / scale - ay;
        float dx = ox * ca - oy * sa, dy = ox * sa + oy * ca;           /* inverse of the rotation above */
        float u = dx / scx - rx, v = dy / scy - ry;
        if (u < 0 || v < 0 || u >= w || v >= h) continue;
        blend(x, y, tx->rgba[(sy + (int)v) * tx->w + sx + (int)u], color);
    }
}
void ge_gfx_draw(ge_tex_t t, int x, int y, int sx, int sy, int w, int h) { ge_gfx_draw_xf(t, sx, sy, w, h, (float)x, (float)y, 0, 0, 1, 1, 0, 0xFFFFFFFFu); }
static float edge(float ax, float ay, float bx, float by, float px, float py) { return (px - ax) * (by - ay) - (py - ay) * (bx - ax); }
static void tri(const float *x, const float *y, int a, int b, int c, uint32_t col)
{
    float x0 = x[a] * scale, y0 = y[a] * scale, x1 = x[b] * scale, y1 = y[b] * scale, x2 = x[c] * scale, y2 = y[c] * scale;
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    if (minx < 0) minx = 0; if (miny < 0) miny = 0; if (maxx > SW) maxx = SW; if (maxy > SH) maxy = SH;
    float area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    for (int py = miny; py < maxy; py++) for (int px = minx; px < maxx; px++) {
        float fx = px + 0.5f, fy = py + 0.5f;
        float w0 = edge(x1, y1, x2, y2, fx, fy) / area, w1 = edge(x2, y2, x0, y0, fx, fy) / area, w2 = edge(x0, y0, x1, y1, fx, fy) / area;
        if (w0 >= 0 && w1 >= 0 && w2 >= 0) blend(px, py, 0xFFFFFFFFu, col);
    }
}
void ge_gfx_flat(int fan, const float *x, const float *y, int n, uint32_t color)
{
    st_flat++; if (getenv("GT_BIG") && st_flat < 20) fprintf(stderr, "flat n %d col %08x x0 %g y0 %g\n", n, color, x[0], y[0]);
    for (int i = 2; i < n; i++) {
        if (fan) tri(x, y, 0, i - 1, i, color); else tri(x, y, i - 2, i - 1, i, color);
    }
}
void ge_gfx_stats(unsigned *uploads, unsigned *evictions, unsigned *bytes, unsigned *h) { *uploads = up; *evictions = 0; *bytes = 0; *h = hits; }
void ge_gfx_frame_end(void) { if (getenv("GT_STAT")) fprintf(stderr, "frame draws %d flat %d area %.0f\n", st_calls, st_flat, st_area); st_calls = 0; st_area = 0; st_flat = 0; }
void ge_gfx_host_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SW, SH);
    for (int i = 0; i < SW * SH; i++) { uint8_t p[3] = {fb[i] & 255, (fb[i] >> 8) & 255, (fb[i] >> 16) & 255}; fwrite(p, 1, 3, f); }
    fclose(f);
}
