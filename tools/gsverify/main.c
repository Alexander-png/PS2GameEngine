// gsverify: uploads one converted PSMT4 and one PSMT8 sprite (gs_convert.py output) and draws them on magenta.
// Used to verify: CLUT4/CLUT8 (CSM1 order) colors, low-nibble-first T4 packing, alpha 0x80 = opaque, row stride padding.
#include <kernel.h>
#include <malloc.h>
#include <tamtypes.h>
#include <string.h>

#include <packet.h>
#include <dma.h>
#include <graph.h>
#include <draw.h>
#include <draw2d.h>
#include <draw_blending.h>

#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>

#include "verify_data.h"

static framebuffer_t frame;
static zbuffer_t z;

static void init_gs(void)
{
    frame.width = 640;
    frame.height = 448;
    frame.mask = 0;
    frame.psm = GS_PSM_32;
    frame.address = graph_vram_allocate(frame.width, frame.height, frame.psm, GRAPH_ALIGN_PAGE);
    z.enable = DRAW_DISABLE;
    z.mask = 1;
    z.method = ZTEST_METHOD_ALLPASS;
    z.zsm = GS_ZBUF_32;
    z.address = graph_vram_allocate(frame.width, frame.height, z.zsm, GRAPH_ALIGN_PAGE);
    graph_set_mode(GRAPH_MODE_AUTO, GRAPH_MODE_NTSC, GRAPH_MODE_FIELD, GRAPH_ENABLE);
    graph_set_screen(0, 0, frame.width, frame.height);
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer_filtered(frame.address, frame.width, frame.psm, 0, 0);
    graph_enable_output();
}

static void send(packet_t *p, qword_t *q)
{
    dma_channel_send_normal(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
    dma_wait_fast();
}

static void send_chain(packet_t *p, qword_t *q)
{
    dma_channel_send_chain(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
    dma_wait_fast();
}

static int log2up(int v)
{
    int l = 0;
    while ((1 << l) < v) l++;
    return l;
}

typedef struct { const void *pix; const void *clut; int w, h, stride, psm, clutw, cluth; int x, y; } tex_t;

static void draw_tex(const tex_t *t, packet_t *p)
{
    int tbw = (t->stride + 127) & ~127; // PSMT8/PSMT4 pages are 128 px wide: TBW must be even
    int tex_addr = graph_vram_allocate(tbw, (t->h + 63) & ~63, GS_PSM_32, GRAPH_ALIGN_PAGE); // generous: PSM32-sized for a T4/T8 image
    int clut_addr = graph_vram_allocate(64, 64, GS_PSM_32, GRAPH_ALIGN_PAGE);

    qword_t *q = p->data;
    q = draw_texture_transfer(q, (void *)t->pix, t->stride, t->h, t->psm, tex_addr, tbw);
    q = draw_texture_transfer(q, (void *)t->clut, t->clutw, t->cluth, GS_PSM_32, clut_addr, 64);
    q = draw_texture_flush(q);
    send_chain(p, q);

    texbuffer_t tb;
    tb.address = tex_addr;
    tb.width = tbw;
    tb.psm = t->psm;
    tb.info.width = log2up(t->w);
    tb.info.height = log2up(t->h);
    tb.info.components = TEXTURE_COMPONENTS_RGBA;
    tb.info.function = TEXTURE_FUNCTION_MODULATE;
    clutbuffer_t cl;
    cl.address = clut_addr;
    cl.psm = GS_PSM_32;
    cl.storage_mode = CLUT_STORAGE_MODE1;
    cl.start = 0;
    cl.load_method = CLUT_LOAD;

    lod_t lod;
    lod.calculation = LOD_USE_K;
    lod.max_level = 0;
    lod.mag_filter = LOD_MAG_NEAREST;
    lod.min_filter = LOD_MIN_NEAREST;
    lod.l = 0;
    lod.k = 0;

    blend_t bl;
    bl.color1 = BLEND_COLOR_SOURCE;
    bl.color2 = BLEND_COLOR_DEST;
    bl.alpha = BLEND_ALPHA_SOURCE;
    bl.color3 = BLEND_COLOR_DEST;
    bl.fixed_alpha = 0;

    q = p->data;
    q = draw_texture_sampling(q, 0, &lod);
    q = draw_texturebuffer(q, 0, &tb, &cl);
    q = draw_alpha_blending(q, 0, &bl);
    send(p, q);

    texrect_t r;
    r.color.r = 0x80; r.color.g = 0x80; r.color.b = 0x80; r.color.a = 0x80; r.color.q = 1.0f;
    r.v0.x = -320.0f + t->x; r.v0.y = -224.0f + t->y; r.v0.z = 0;
    r.v1.x = r.v0.x + t->w; r.v1.y = r.v0.y + t->h; r.v1.z = 0;
    r.t0.u = 0; r.t0.v = 0; r.t1.u = t->w; r.t1.v = t->h;
    q = p->data;
    q = draw_rect_textured(q, 0, &r);
    q = draw_finish(q);
    send(p, q);
    draw_wait_finish();
}

int main(void)
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    init_gs();

    packet_t *p = packet_init(4000, PACKET_NORMAL);
    qword_t *q = p->data;
    q = draw_setup_environment(q, 0, &frame, &z);
    q = draw_primitive_xyoffset(q, 0, 2048 - 320, 2048 - 224);
    // clear to magenta
    q = draw_disable_tests(q, 0, &z);
    q = draw_clear(q, 0, 2048.0f - 320, 2048.0f - 224, frame.width, frame.height, 255, 0, 255);
    q = draw_finish(q);
    send(p, q);
    draw_wait_finish();

    tex_t a = { t4_pix, t4_clut, T4_W, T4_H, T4_STRIDE, GS_PSM_4, 8, 2, 8, 8 };
    tex_t b = { t8_pix, t8_clut, T8_W, T8_H, T8_STRIDE, GS_PSM_8, 16, 16, 8, 8 + T4_H + 8 };
    draw_tex(&a, p);
    draw_tex(&b, p);
    SleepThread();
    return 0;
}
