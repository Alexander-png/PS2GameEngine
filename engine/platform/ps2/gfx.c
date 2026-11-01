/* PS2 GS graphics module: indexed textures (PSMT4/PSMT8 + CLUT) drawn as alpha-blended sprites, 2x integer scale. */
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <dma.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>
#include <gs_gp.h>
#include <gs_privileged.h>
#include <draw.h>
#include <graph.h>
#include <packet.h>
#include "ge/gfx.h"

#define MAXTEX 512
#define PKT_QW 16384
/* Textures are registered by ge_gfx_tex_create and made resident in VRAM lazily on first draw; when VRAM is full the least
 * recently drawn textures are evicted (pending draws are flushed first, so overwriting is safe). The pixel/CLUT memory
 * passed in must stay valid until ge_gfx_tex_free_all. */
typedef struct { ge_texdesc_t d; int tbw, tw, th, psm, np; int page, clut, res, refs; unsigned last; } vram_t;

static framebuffer_t fbs[2]; static zbuffer_t zb;
static int back = 1;          /* index of the buffer being drawn; the other one is on screen (double buffered, flipped at vblank) */
static int SW, SH, VWc = 320, VHc = 240;
static float scale = 2.0f;
static vram_t V[MAXTEX]; static int nt;
/* VRAM map (8 KB pages, 512 total): framebuffer, then a CLUT region (16 pages = 512 blocks of 64 words), then texture pages. */
#define VPAGES 512
#define CLUT_PAGES 16
static unsigned char pused[VPAGES], cused[CLUT_PAGES * 32];
static int tex_first, clut_first;
static unsigned frame_no, st_up, st_ev, st_bytes, st_hit;
static int cur = -1;
static packet_t *pk; static qword_t *q;
static const int cpsm = 0;
static uint32_t cur_color; static int col_ok;      /* col_ok: the GS RGBAQ holds the texture-modulation form of cur_color (flat polygons write raw RGB instead) */
static int cl_on; static unsigned cl_key;     /* CLAMP_1 region clamp (strongly magnified / rotated sprites must not sample their atlas neighbours) */
/* GS UV fields are 14 bits (12.4 fixed): the far edge of a texture that is 1024 texels tall/wide would be 1024*16 = 0x4000 and wrap to 0 */
static inline int uvfix(int v) { return v > 0x3FFF ? 0x3FFF : v; }
static int ilog2(int v) { int n = 0; while ((1 << n) < v) n++; return n; }
static void send_normal(qword_t *a, qword_t *b) { dma_channel_send_normal(DMA_CHANNEL_GIF, a, b - a, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0); }
static void send_chain(qword_t *a, qword_t *b) { dma_channel_send_chain(DMA_CHANNEL_GIF, a, b - a, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0); }
static void flush_mid(void)
{
    if (q == pk->data) return;
    send_normal(pk->data, q);
    q = pk->data;
}
void ge_gfx_tex_release(ge_tex_t t) { if (t >= 0 && t < nt && V[t].refs > 0) V[t].refs--; }
void ge_gfx_stats(unsigned *uploads, unsigned *evictions, unsigned *bytes, unsigned *hits) { *uploads = st_up; *evictions = st_ev; *bytes = st_bytes; *hits = st_hit; }

static int find_pages(int n)
{
    int run = 0;
    for (int i = tex_first; i < VPAGES; i++) {
        run = pused[i] ? 0 : run + 1;
        if (run == n) return i - n + 1;
    }
    return -1;
}
static int find_clut(int n)
{
    for (int i = 0; i + n <= CLUT_PAGES * 32; i += n) {
        int ok = 1;
        for (int k = 0; k < n; k++) if (cused[i + k]) { ok = 0; break; }
        if (ok) return i;
    }
    return -1;
}
static void evict(int i)
{
    vram_t *v = &V[i];
    int t4 = v->d.fmt == GE_FMT_I4;
    memset(pused + v->page, 0, v->np);
    memset(cused + (v->clut - clut_first * 32) , 0, t4 ? 1 : 4);
    v->res = 0; st_ev++;
    if (cur == i) cur = -1;
}
static int make_resident(int t)
{
    vram_t *v = &V[t];
    int t4 = v->d.fmt == GE_FMT_I4, cn = t4 ? 1 : 4, pg, cb;
    flush_mid();                       /* queued draws may still read pages we are about to reuse */
    for (;;) {
        pg = find_pages(v->np); cb = find_clut(cn);
        if (pg >= 0 && cb >= 0) break;
        int lru = -1;        /* victim: unreferenced (cached) textures first, then least recently drawn */
        for (int i = 0; i < nt; i++) {
            if (!V[i].res) continue;
            if (lru < 0 || (V[i].refs > 0) < (V[lru].refs > 0) ||
                ((V[i].refs > 0) == (V[lru].refs > 0) && (int)(V[i].last - V[lru].last) < 0)) lru = i;
        }
        if (lru < 0) return -1;
        evict(lru);
    }
    const void *clut = v->d.clut, *pix = v->d.pixels;
    if (v->d.load && v->d.load(v->d.user, &clut, &pix) != 0) return -1;
    memset(pused + pg, 1, v->np); memset(cused + cb, 1, cn);
    v->page = pg; v->clut = clut_first * 32 + cb;           /* CLUT in blocks */
    FlushCache(0);
    qword_t *p = pk->data;
    p = draw_texture_transfer(p, (void *)clut, t4 ? 8 : 16, t4 ? 2 : 16, GS_PSM_32, v->clut * 64, 64);
    p = draw_texture_flush(p);
    send_chain(pk->data, p);
    p = pk->data;
    /* the pixels go up one GS page row at a time (64 rows for 8-bit, 128 for 4-bit): every transfer starts page aligned and stays small;
     * one big transfer of a tall 8-bit atlas came out scrambled in its lower rows */
    {
        int prow = t4 ? 128 : 64, pw = v->tbw / 128;
        for (int r0 = 0; r0 < v->d.h; r0 += prow) {
            int hh = v->d.h - r0 < prow ? v->d.h - r0 : prow;
            const uint8_t *src = (const uint8_t *)pix + (size_t)r0 * v->d.stride / (t4 ? 2 : 1);
            p = pk->data;
            p = draw_texture_transfer(p, (void *)src, v->d.stride, hh, v->psm, (pg + (r0 / prow) * pw) * 2048, v->tbw);
            p = draw_texture_flush(p);
            send_chain(pk->data, p);
        }
        p = pk->data;
    }
    v->res = 1; st_up++; st_bytes += v->d.pixels_len;
    return 0;
}



int ge_gfx_init(int w, int h)
{
    SW = w; SH = h; scale = 2.0f; VWc = w / 2; VHc = h / 2;
    int fbpages = ((SW + 63) / 64) * ((SH + 31) / 32);
    for (int k = 0; k < 2; k++) { fbs[k].width = SW; fbs[k].height = SH; fbs[k].mask = 0; fbs[k].psm = GS_PSM_32; fbs[k].address = k * fbpages * 2048; }
    clut_first = 2 * fbpages; tex_first = clut_first + CLUT_PAGES;
    zb.enable = 0; zb.mask = 0; zb.method = 0; zb.zsm = 0; zb.address = 0;
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    graph_initialize(0, SW, SH, GS_PSM_32, 0, 0);
    pk = packet_init(PKT_QW, PACKET_NORMAL);
    q = pk->data;
    q = draw_setup_environment(q, 0, &fbs[0], &zb);
    q = draw_clear(q, 0, 0, 0, (float)SW, (float)SH, 0, 0, 0);
    send_normal(pk->data, q);
    q = pk->data;
    q = draw_setup_environment(q, 0, &fbs[1], &zb);
    q = draw_clear(q, 0, 0, 0, (float)SW, (float)SH, 0, 0, 0);
    send_normal(pk->data, q);
    q = pk->data;
    return 0;
}

void ge_gfx_set_view(int vw, int vh) { if (vw > 0) { VWc = vw; VHc = vh; scale = (float)SW / vw; } }
void ge_gfx_get_view(int *vw, int *vh) { *vw = VWc; *vh = VHc; }

ge_tex_t ge_gfx_tex_create(const ge_texdesc_t *d)
{
    int t4 = d->fmt == GE_FMT_I4, slot = -1;
    if (d->key)
        for (int i = 0; i < nt; i++) if (V[i].d.key == d->key) { V[i].refs++; st_hit++; return i; }
    if (nt < MAXTEX) slot = nt++;
    else {                                   /* reuse an unreferenced slot (evicting it from VRAM if resident) */
        for (int i = 0; i < nt; i++) if (!V[i].refs && (slot < 0 || (int)(V[i].last - V[slot].last) < 0)) slot = i;
        if (slot < 0) return -1;
        if (V[slot].res) evict(slot);
    }
    vram_t *v = &V[slot];
    v->d = *d; v->res = 0; v->last = 0; v->refs = 1;
    v->psm = t4 ? GS_PSM_4 : GS_PSM_8;
    v->tbw = (d->stride + 127) & ~127;       /* T4/T8 texture buffer width must be a multiple of 128 px */
    v->tw = ilog2(v->tbw); v->th = ilog2(d->h);
    v->np = (v->tbw / 128) * ((d->h + (t4 ? 127 : 63)) / (t4 ? 128 : 64));
    if (v->np > VPAGES - tex_first) { v->refs = 0; v->d.key = 0; return -1; }
    return slot;
}

void ge_gfx_tex_prefetch(ge_tex_t t)
{
    if (t < 0 || t >= nt) return;
    if (!V[t].res) make_resident(t);
    V[t].last = frame_no;
}

void ge_gfx_frame_begin(uint32_t bg)
{
    q = pk->data; cur = -1; cur_color = 0xFFFFFFFFu; col_ok = 1; frame_no++; cl_on = 0;
    q = draw_setup_environment(q, 0, &fbs[back], &zb);
    q = draw_disable_tests(q, 0, &zb);
    q = draw_clear(q, 0, 0, 0, (float)SW, (float)SH, bg & 255, (bg >> 8) & 255, (bg >> 16) & 255);
    PACK_GIFTAG(q, GIF_SET_TAG(4, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_CLAMP(0, 0, 0, 0, 0, 0), GS_REG_CLAMP_1); q++;
    PACK_GIFTAG(q, GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0), GS_REG_RGBAQ); q++;
    PACK_GIFTAG(q, GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA_1); q++;
    PACK_GIFTAG(q, GS_SET_TEX1(0, 0, 0, 0, 0, 0, 0), GS_REG_TEX1_1); q++;
}


void ge_gfx_draw_xf(ge_tex_t t, int sx, int sy, int w, int h, float ax, float ay, float rx, float ry,
                    float scx, float scy, float rot, uint32_t color)
{
    if (t < 0 || scx == 0 || scy == 0) return;
    float px[4], py[4];
    float sfx = 1, sfy = 1, cu0 = 0, cv0 = 0, cu1 = 1, cv1 = 1;
    int sprite = rot == 0 && scx > 0 && scy > 0;
    if (sprite) {
        px[0] = (ax + rx * scx) * scale; py[0] = (ay + ry * scy) * scale;
        px[1] = px[0] + w * scx * scale; py[1] = py[0] + h * scy * scale;
        if (px[1] <= 0 || py[1] <= 0 || px[0] >= SW || py[0] >= SH) return;
        /* GS XYZ is 16 bit (offset 2048): hugely scaled sprites (fader 'pixblk') must be clipped to the screen, UVs follow */
        sfx = (px[1] - px[0]); sfy = (py[1] - py[0]);
        if (px[0] < -8) { cu0 = (-8 - px[0]) / sfx; px[0] = -8; }
        if (py[0] < -8) { cv0 = (-8 - py[0]) / sfy; py[0] = -8; }
        if (px[1] > SW + 8) { cu1 = 1.0f - (px[1] - (SW + 8)) / sfx; px[1] = SW + 8; }
        if (py[1] > SH + 8) { cv1 = 1.0f - (py[1] - (SH + 8)) / sfy; py[1] = SH + 8; }
    } else {
        float a = rot * 0.017453292519943295f, ca = cosf(a), sa = sinf(a);
        static const int cu[4] = {0, 1, 0, 1}, cv[4] = {0, 0, 1, 1};
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (int i = 0; i < 4; i++) {
            float dx = (rx + cu[i] * w) * scx, dy = (ry + cv[i] * h) * scy;
            px[i] = (ax + dx * ca + dy * sa) * scale; py[i] = (ay - dx * sa + dy * ca) * scale;
            if (px[i] < minx) minx = px[i]; if (px[i] > maxx) maxx = px[i];
            if (py[i] < miny) miny = py[i]; if (py[i] > maxy) maxy = py[i];
        }
        if (maxx <= 0 || maxy <= 0 || minx >= SW || miny >= SH) return;
    }
    if (!V[t].res && make_resident(t) < 0) return;
    V[t].last = frame_no;
    if ((q - pk->data) > PKT_QW - 16) flush_mid();
    int newtex = t != cur, newcol = !col_ok || color != cur_color;
    /* strongly magnified tiny sprites (white-fade pixel block): one constant-UV quad per texel; GS UV interpolation over hundreds of pixels is too coarse */
    int pertex = sprite && (scx >= 3 || scy >= 3) && w * h <= 64;
    int needcl = !pertex && (!sprite || scx >= 3 || scy >= 3);
    unsigned clk = ((unsigned)sx << 20) ^ ((unsigned)sy << 8) ^ ((unsigned)w << 3) ^ ((unsigned)h << 14) ^ (unsigned)t * 2654435761u;
    int newcl = needcl ? (!cl_on || clk != cl_key || newtex) : cl_on;
    int nr = 1 + (newtex ? 1 : 0) + (newcol ? 1 : 0) + (newcl ? 1 : 0) + (pertex ? 4 * w * h : sprite ? 4 : 8);
    PACK_GIFTAG(q, GIF_SET_TAG(nr, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD); q++;
    if (newcl) {
        if (needcl) { PACK_GIFTAG(q, GS_SET_CLAMP(2, 2, sx, sx + w - 1, sy, sy + h - 1), GS_REG_CLAMP_1); cl_on = 1; cl_key = clk; }
        else { PACK_GIFTAG(q, GS_SET_CLAMP(0, 0, 0, 0, 0, 0), GS_REG_CLAMP_1); cl_on = 0; }
        q++;
    }
    if (newtex) {
        cur = t; vram_t *v = &V[t];
        PACK_GIFTAG(q, GS_SET_TEX0(v->page * 32, v->tbw >> 6, v->psm, v->tw, v->th, 1, 0, v->clut, cpsm, 0, 0, 1), GS_REG_TEX0_1); q++;
    }
    if (newcol) {
        cur_color = color; col_ok = 1;
        int r = ((color & 255) * 128 + 127) / 255, g = (((color >> 8) & 255) * 128 + 127) / 255,
            b = (((color >> 16) & 255) * 128 + 127) / 255, al = ((color >> 24) * 128 + 127) / 255;
        PACK_GIFTAG(q, GS_SET_RGBAQ(r, g, b, al, 0), GS_REG_RGBAQ); q++;
    }
    PACK_GIFTAG(q, GS_SET_PRIM(sprite ? PRIM_SPRITE : PRIM_TRIANGLE_STRIP, 0, 1, 0, 1, 0, 1, 0, 0), GS_REG_PRIM); q++;
    if (pertex) {
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
            /* texel edges in unclipped space, then clamped to the clipped rect */
            float ox = px[0] - cu0 * sfx, oy = py[0] - cv0 * sfy;
            float x0 = ox + sfx * i / w, x1 = ox + sfx * (i + 1) / w, y0 = oy + sfy * j / h, y1 = oy + sfy * (j + 1) / h;
            if (x0 < px[0]) x0 = px[0]; if (x1 > px[1]) x1 = px[1]; if (x1 < x0) x1 = x0;
            if (y0 < py[0]) y0 = py[0]; if (y1 > py[1]) y1 = py[1]; if (y1 < y0) y1 = y0;
            int u = ((sx + i) << 4) + 8, v = ((sy + j) << 4) + 8;
            PACK_GIFTAG(q, GS_SET_UV(u, v), GS_REG_UV); q++;
            PACK_GIFTAG(q, GS_SET_XYZ((int)((2048 + x0) * 16), (int)((2048 + y0) * 16), 0), GS_REG_XYZ2); q++;
            PACK_GIFTAG(q, GS_SET_UV(u, v), GS_REG_UV); q++;
            PACK_GIFTAG(q, GS_SET_XYZ((int)((2048 + x1) * 16), (int)((2048 + y1) * 16), 0), GS_REG_XYZ2); q++;
        }
    } else if (sprite) {
        PACK_GIFTAG(q, GS_SET_UV(uvfix((int)((sx + cu0 * w) * 16)), uvfix((int)((sy + cv0 * h) * 16))), GS_REG_UV); q++;
        PACK_GIFTAG(q, GS_SET_XYZ((int)((2048 + px[0]) * 16), (int)((2048 + py[0]) * 16), 0), GS_REG_XYZ2); q++;
        PACK_GIFTAG(q, GS_SET_UV(uvfix((int)((sx + cu1 * w) * 16)), uvfix((int)((sy + cv1 * h) * 16))), GS_REG_UV); q++;
        PACK_GIFTAG(q, GS_SET_XYZ((int)((2048 + px[1]) * 16), (int)((2048 + py[1]) * 16), 0), GS_REG_XYZ2); q++;
    } else {
        static const int cu[4] = {0, 1, 0, 1}, cv[4] = {0, 0, 1, 1};
        for (int i = 0; i < 4; i++) {
            PACK_GIFTAG(q, GS_SET_UV(uvfix((sx + cu[i] * w) << 4), uvfix((sy + cv[i] * h) << 4)), GS_REG_UV); q++;
            PACK_GIFTAG(q, GS_SET_XYZ((int)((2048 + px[i]) * 16), (int)((2048 + py[i]) * 16), 0), GS_REG_XYZ2); q++;
        }
    }
}

void ge_gfx_flat(int fan, const float *x, const float *y, int n, uint32_t color)
{
    if (n < 3 || n > 64) return;
    if ((q - pk->data) > PKT_QW - 16 - n * 2) flush_mid();
    PACK_GIFTAG(q, GIF_SET_TAG(2 + n, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD); q++;
    cur_color = color; col_ok = 0;     /* raw RGB (no 0x80 modulation scale) is in RGBAQ now: the next textured draw must rewrite it */
    /* untextured: RGB goes to the framebuffer as is (the 0x80 = 1.0 scale only applies when modulating a texture); alpha 0x80 = opaque */
    int r = color & 255, g = (color >> 8) & 255, b = (color >> 16) & 255, al = ((color >> 24) * 128 + 127) / 255;
    PACK_GIFTAG(q, GS_SET_RGBAQ(r, g, b, al, 0), GS_REG_RGBAQ); q++;
    PACK_GIFTAG(q, GS_SET_PRIM(fan ? PRIM_TRIANGLE_FAN : PRIM_TRIANGLE_STRIP, 0, 0, 0, 1, 0, 1, 0, 0), GS_REG_PRIM); q++;
    for (int i = 0; i < n; i++) {
        PACK_GIFTAG(q, GS_SET_XYZ((int)((2048 + x[i] * scale) * 16), (int)((2048 + y[i] * scale) * 16), 0), GS_REG_XYZ2); q++;
    }
}

void ge_gfx_draw(ge_tex_t t, int x, int y, int sx, int sy, int w, int h)
{
    ge_gfx_draw_xf(t, sx, sy, w, h, (float)x, (float)y, 0, 0, 1, 1, 0, 0xFFFFFFFFu);
}

void ge_gfx_frame_end(void)
{
    *GS_REG_CSR = 2;
    PACK_GIFTAG(q, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, 0, GS_REG_FINISH); q++;
    send_normal(pk->data, q);
    q = pk->data;
    while (!(*GS_REG_CSR & 2)) ;
    graph_wait_vsync();                                           /* show the finished buffer at vblank: no tearing, no half-cleared frames on screen */
    graph_set_framebuffer_filtered(fbs[back].address, SW, GS_PSM_32, 0, 0);
    back ^= 1;
}
