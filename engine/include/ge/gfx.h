/* ge/gfx.h - platform graphics interface. Core code only talks to this; each platform module implements it. */
#ifndef GE_GFX_H
#define GE_GFX_H
#include <stdint.h>

enum { GE_FMT_I4 = 0, GE_FMT_I8 = 1 };            /* indexed texture formats produced by the converter */

typedef struct {
    int fmt;                  /* GE_FMT_* */
    int w, h;                 /* visible size in texels */
    int stride;               /* row stride in texels (padded by the converter) */
    const void *clut;         /* RGBA32 palette, 16 or 256 entries, already in the platform's expected order */
    int clut_len;
    const void *pixels;       /* stride*h texels, packed (I4: 2 px/byte, first pixel in the low nibble) */
    int pixels_len;
    /* streaming: when `load` is set, clut/pixels above are ignored; the platform calls load(user, &clut, &pixels) each time the
     * texture has to be (re)uploaded and the returned memory only needs to stay valid until the call to the platform returns. */
    int (*load)(void *user, const void **clut, const void **pixels);   /* 0 = ok */
    void *user;               /* must stay valid for the lifetime of the pack (shared by every room that uses the texture) */
    uint32_t key;             /* nonzero: stable asset id; a texture with the same key that is still cached in VRAM is reused */
} ge_texdesc_t;

typedef int ge_tex_t;     /* handle, -1 = invalid */

int      ge_gfx_init(int screen_w, int screen_h);                 /* 0 on success */
void     ge_gfx_set_tilt(float degrees);                          /* rotate everything drawn about the view centre (GameMaker view_angle, counter-clockwise) */
void     ge_gfx_set_view(int view_w, int view_h);                 /* game view size in game px; scale = screen_w / view_w */
void     ge_gfx_get_view(int *view_w, int *view_h);
ge_tex_t ge_gfx_tex_create(const ge_texdesc_t *d);
void     ge_gfx_tex_release(ge_tex_t t);                          /* drop one reference (room freed); stays cached in VRAM until space is needed */
void     ge_gfx_tex_prefetch(ge_tex_t t);                         /* make resident now (call outside a frame) */
void     ge_gfx_frame_begin(uint32_t bg_rgba);
/* draw source rect (sx,sy,w,h) of texture t at destination (x,y); the game->screen scale is fixed by the platform */
void     ge_gfx_draw(ge_tex_t t, int x, int y, int sx, int sy, int w, int h);
/* transformed draw: anchor (ax,ay) in game px; the source rect's top-left sits at anchor + R(S*(rx,ry)); S = scale (may be
 * negative = flip), R = rotation by rot_deg counter-clockwise on screen; color = GM ABGR (alpha in the top byte) modulates. */
void     ge_gfx_draw_xf(ge_tex_t t, int sx, int sy, int w, int h, float ax, float ay, float rx, float ry,
                        float scx, float scy, float rot_deg, uint32_t color);
/* untextured polygon in game px: fan = 0 triangle strip (a quad is 0,1,2,3 in Z order), 1 triangle fan; n <= 64; color = ABGR (alpha in the top byte) */
void     ge_gfx_flat(int fan, const float *x, const float *y, int n, uint32_t color);
/* screen capture (GM sprite_create_from_screen): copies the region (x,y,*w,*h) of the last finished frame, in screen pixels, into a small
 * texture drawable with ge_gfx_draw_xf (sx=sy=0, w, h). The size is clamped to what fits (64x64) and written back. -1 = no free capture slot. */
ge_tex_t ge_gfx_capture(int x, int y, int *w, int *h);
void     ge_gfx_capture_free(ge_tex_t t);
void     ge_gfx_stats(unsigned *uploads, unsigned *evictions, unsigned *bytes, unsigned *hits);   /* texture residency counters (hits = created textures already cached) */
void     ge_gfx_frame_end(void);                                  /* submit + wait for the GS/GPU to finish */
#endif
