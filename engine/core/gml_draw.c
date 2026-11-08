/* gml_draw.c - GML drawing builtins and the draw hooks of the frame loop, on top of ge_gfx + the pack's sprite/background/font tables.
 * Only linked into builds with graphics (the host test harness keeps the silent weak stubs of gml_lib.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ge/gfx.h"
#include "ge/room.h"
#include "ge/log.h"
#include "gml/gml_rt.h"

#define NONE 0xFFFFFFFFu
#define PI_F 3.14159265f

static ge_pack_t *pack;
static ge_room_t *room;
static int next_item, cam_x, cam_y;
static uint32_t dcolor = 0, dfont = 0;           /* draw color (GM BGR int), font index */
static float dalpha = 1.0f;
static int halign, valign;

static uint32_t abgr(int bgr, float alpha)
{
    int a = (int)(alpha * 255.0f + 0.5f);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return ((uint32_t)bgr & 0xFFFFFF) | ((uint32_t)a << 24);
}

/* ---------------- frame hooks ---------------- */
static int room_slots, dyn_done;                       /* background slots defined by the room / dynamic backgrounds already drawn this frame */
static void draw_dyn_bgs(int foreground);
static void h_room_start(int r)
{
    room_slots = 0;
    if (room) { ge_room_free(room); room = NULL; }
    if (r < 0 || r >= gm_nrooms) return;
    int idx = ge_pack_find(pack, gm_rooms[r].name);
    if (idx < 0) { gm_logf("room %s not in pack", gm_rooms[r].name); return; }
    room = ge_room_load_index(pack, idx);
    if (!room) { gm_logf("room %s failed to load", gm_rooms[r].name); return; }
    const gm_roomview *v = &gm_rooms[r].views[0];
    if (v->enabled) ge_gfx_set_view(v->w, v->h);
    /* room background layers: background_visible[slot] starts as in the room definition; the game flips it (lamps, lights) */
    int vid = gm_vid("background_visible");
    if (vid >= 0) {
        for (int k = 0, n = ge_room_item_count(room); k < n; k++) {
            int on = 0, slot = ge_room_item_bgslot(room, k, &on);
            if (slot >= 0 && slot < 8) { gm_seta(gm_global_inst, vid, gm_real(slot), gm_real(on)); room_slots |= 1 << slot; }
        }
    }
}
static void h_room_end(int r)
{   /* the next room starts with every background slot off (its Create events, which run before room_start, may switch some on) */
    (void)r;
    int vid = gm_vid("background_visible");
    if (vid >= 0) for (int k = 0; k < 8; k++) gm_seta(gm_global_inst, vid, gm_real(k), gm_real(0));
}
static int gint(const char *n, int idx) { return gm_to_int(gm_geta(gm_global_inst, gm_vid(n), gm_real(idx))); }
static void h_draw_begin(void)
{
    gm_camera(&cam_x, &cam_y);
    {   /* view zoom (cutscene effects change view_wview/hview): the game->screen scale follows it */
        extern int gm_camera_view(int *w, int *h);
        static int zw, zh; int vw, vh, cw, ch;
        if (gm_camera_view(&vw, &vh) && (vw != zw || vh != zh)) { ge_gfx_get_view(&cw, &ch); if (vw != cw || vh != ch) ge_gfx_set_view(vw, vh); zw = vw; zh = vh; }
    }
    {   extern float gm_camera_angle(void); ge_gfx_set_tilt(gm_camera_angle()); }
    next_item = 0; dyn_done = 0;
    ge_gfx_frame_begin(room ? ge_room_bg_color(room) : 0x80000000u);
}
static void h_draw_layers(int key)
{
    if (!dyn_done) { dyn_done = 1; draw_dyn_bgs(0); }       /* backgrounds the game switches on by script (background_index[]), behind everything */
    if (!room) return;
    int n = ge_room_item_count(room);
    static int vid_bgv = -2;
    if (vid_bgv == -2) vid_bgv = gm_vid("background_visible");
    while (next_item < n && ge_room_item_key(room, next_item) <= key) {
        int slot = ge_room_item_bgslot(room, next_item, NULL);
        if (slot >= 0 && vid_bgv >= 0 && !gm_truth(gm_geta(gm_global_inst, vid_bgv, gm_real(slot)))) { next_item++; continue; }
        ge_room_draw_item(room, next_item++, cam_x, cam_y);
    }
}
/* backgrounds set by game code: background_visible/index/x/y/htiled/vtiled/foreground/alpha/hspeed/vspeed [slot] for slots the room does not define */
static void draw_dyn_bgs(int foreground)
{
    static int v_vis = -2, v_idx, v_x, v_y, v_ht, v_vt, v_fg, v_al, v_hs, v_vs;
    if (v_vis == -2) {
        v_vis = gm_vid("background_visible"); v_idx = gm_vid("background_index"); v_x = gm_vid("background_x"); v_y = gm_vid("background_y");
        v_ht = gm_vid("background_htiled"); v_vt = gm_vid("background_vtiled"); v_fg = gm_vid("background_foreground");
        v_al = gm_vid("background_alpha"); v_hs = gm_vid("background_hspeed"); v_vs = gm_vid("background_vspeed");
    }
    if (v_vis < 0 || v_idx < 0) return;
    int vw, vh; ge_gfx_get_view(&vw, &vh);
    for (int k = 0; k < 8; k++) {
        if ((room_slots >> k & 1) || !gm_truth(gm_geta(gm_global_inst, v_vis, gm_real(k)))) continue;
        int fg = v_fg >= 0 && gm_truth(gm_geta(gm_global_inst, v_fg, gm_real(k)));
        if (fg != foreground) continue;
        int bi = gm_to_int(gm_geta(gm_global_inst, v_idx, gm_real(k)));
        const ge_bg_t *b = bi >= 0 ? ge_pack_bg(pack, bi) : NULL;
        if (!b || b->asset == NONE || !b->w || !b->h) continue;
        float x = v_x >= 0 ? (float)gm_to_real(gm_geta(gm_global_inst, v_x, gm_real(k))) : 0, y = v_y >= 0 ? (float)gm_to_real(gm_geta(gm_global_inst, v_y, gm_real(k))) : 0;
        int ht = v_ht >= 0 && gm_truth(gm_geta(gm_global_inst, v_ht, gm_real(k))), vt = v_vt >= 0 && gm_truth(gm_geta(gm_global_inst, v_vt, gm_real(k)));
        float al = v_al >= 0 ? (float)gm_to_real(gm_geta(gm_global_inst, v_al, gm_real(k))) : 1; if (al <= 0 && v_al >= 0) al = 1;
        int ox = (int)(b->pad & 0xFFFF), oy = (int)(b->pad >> 16), pw = b->w + ox, ph = b->h + oy;     /* tile period (untrimmed size) */
        float x0 = x, y0 = y;
        if (ht) x0 = x + floorf((cam_x - x) / pw) * pw;
        if (vt) y0 = y + floorf((cam_y - y) / ph) * ph;
        ge_tex_t t = ge_pack_tex(pack, b->asset);
        for (float yy = y0; ; yy += ph) {
            for (float xx = x0; ; xx += pw) {
                ge_gfx_draw_xf(t, 0, 0, b->w, b->h, xx + ox - cam_x, yy + oy - cam_y, 0, 0, 1, 1, 0, abgr(0xFFFFFF, al));
                if (!ht || xx + pw >= cam_x + vw) break;
            }
            if (!vt || yy + ph >= cam_y + vh) break;
        }
        {   /* speeds advance once per drawn frame */
            {
                if (v_hs >= 0) { float hs = (float)gm_to_real(gm_geta(gm_global_inst, v_hs, gm_real(k))); if (hs != 0 && v_x >= 0) gm_seta(gm_global_inst, v_x, gm_real(k), gm_real(x + hs)); }
                if (v_vs >= 0) { float vs = (float)gm_to_real(gm_geta(gm_global_inst, v_vs, gm_real(k))); if (vs != 0 && v_y >= 0) gm_seta(gm_global_inst, v_y, gm_real(k), gm_real(y + vs)); }
            }
        }
    }
}
static void h_draw_end(void)
{
    draw_dyn_bgs(1);
    ge_gfx_frame_end();
    ge_pack_frame_end(pack);
}

/* ---------------- sprites ---------------- */
static const ge_frame_t *frame_of(int spr, gm_real_t sub, const ge_sprite_t **sp)
{
    *sp = ge_pack_sprite(pack, spr);
    if (!*sp || !(*sp)->nf) return NULL;
    int n = (*sp)->nf, f = (int)floorf((float)sub) % n;
    if (f < 0) f += n;
    const ge_frame_t *fr = ge_pack_frame(pack, *sp, f);
    return (fr->asset == NONE || !fr->w) ? NULL : fr;
}
static void spr_draw(int spr, gm_real_t sub, float x, float y, float xs, float ys, float rot, uint32_t col)
{
    const ge_sprite_t *sp;
    const ge_frame_t *fr = frame_of(spr, sub, &sp);
    if (!fr) return;
    ge_tex_t t = ge_pack_tex(pack, fr->asset);
    ge_gfx_draw_xf(t, 0, 0, fr->w, fr->h, x - cam_x, y - cam_y, fr->rx, fr->ry, xs, ys, rot, col);
}
/* part of the sprite image (rect in sprite pixels) with its top-left at (x,y) */
static void spr_part(int spr, gm_real_t sub, int l, int t, int w, int h, float x, float y, float xs, float ys, uint32_t col)
{
    const ge_sprite_t *sp;
    const ge_frame_t *fr = frame_of(spr, sub, &sp);
    if (!fr || spr >= gm_nsprites) return;
    int tx = fr->rx + gm_sprites[spr].ox, ty = fr->ry + gm_sprites[spr].oy;       /* crop position inside the sprite image */
    int sl = l > tx ? l : tx, st = t > ty ? t : ty;
    int sr = (l + w) < (tx + fr->w) ? (l + w) : (tx + fr->w), sb = (t + h) < (ty + fr->h) ? (t + h) : (ty + fr->h);
    if (sr <= sl || sb <= st) return;
    ge_tex_t tex = ge_pack_tex(pack, fr->asset);
    ge_gfx_draw_xf(tex, sl - tx, st - ty, sr - sl, sb - st, x + (sl - l) * xs - cam_x, y + (st - t) * ys - cam_y, 0, 0, xs, ys, 0, col);
}
static gm_real_t subimg(gm_inst *self, gm_val v)
{
    gm_real_t s = gm_to_real(v);
    return s < 0 ? (self ? self->image_index : 0) : s;
}

GMB(draw_sprite) { (void)other; spr_draw(A_INT(0), subimg(self, GM_ARG(1)), A_REAL(2), A_REAL(3), 1, 1, 0, abgr(0xFFFFFF, dalpha)); return GM_UNDEF_V; }
GMB(draw_sprite_ext)
{
    (void)other;
    spr_draw(A_INT(0), subimg(self, GM_ARG(1)), A_REAL(2), A_REAL(3), A_REAL(4), A_REAL(5), A_REAL(6), abgr(A_INT(7), A_REAL(8)));
    return GM_UNDEF_V;
}
GMB(draw_self)
{
    (void)other; (void)argc; (void)argv;
    if (self && self->sprite >= 0)
        spr_draw(self->sprite, self->image_index, self->x, self->y, self->image_xscale, self->image_yscale, self->image_angle, abgr(self->image_blend, self->image_alpha));
    return GM_UNDEF_V;
}
GMB(draw_sprite_part)
{
    (void)other;
    spr_part(A_INT(0), subimg(self, GM_ARG(1)), A_INT(2), A_INT(3), A_INT(4), A_INT(5), A_REAL(6), A_REAL(7), 1, 1, abgr(0xFFFFFF, dalpha));
    return GM_UNDEF_V;
}
GMB(draw_sprite_part_ext)
{
    (void)other;
    spr_part(A_INT(0), subimg(self, GM_ARG(1)), A_INT(2), A_INT(3), A_INT(4), A_INT(5), A_REAL(6), A_REAL(7), A_REAL(8), A_REAL(9), abgr(A_INT(10), A_REAL(11)));
    return GM_UNDEF_V;
}
GMB(draw_sprite_stretched)
{
    (void)other;
    int spr = A_INT(0);
    const ge_sprite_t *sp;
    const ge_frame_t *fr = frame_of(spr, subimg(self, GM_ARG(1)), &sp);
    if (!fr || spr >= gm_nsprites) return GM_UNDEF_V;
    float sw = (float)gm_sprites[spr].w, sh = (float)gm_sprites[spr].h;
    float xs = sw > 0 ? A_REAL(4) / sw : 1, ys = sh > 0 ? A_REAL(5) / sh : 1;
    ge_tex_t t = ge_pack_tex(pack, fr->asset);
    ge_gfx_draw_xf(t, 0, 0, fr->w, fr->h, A_REAL(2) - cam_x, A_REAL(3) - cam_y, fr->rx + gm_sprites[spr].ox, fr->ry + gm_sprites[spr].oy, xs, ys, 0, abgr(0xFFFFFF, dalpha));
    return GM_UNDEF_V;
}
GMB(draw_background)
{
    (void)self; (void)other;
    const ge_bg_t *b = ge_pack_bg(pack, A_INT(0));
    if (!b || b->asset == NONE) return GM_UNDEF_V;
    ge_gfx_draw_xf(ge_pack_tex(pack, b->asset), 0, 0, b->w, b->h, A_REAL(1) + (b->pad & 0xFFFF) - cam_x, A_REAL(2) + (b->pad >> 16) - cam_y, 0, 0, 1, 1, 0, abgr(0xFFFFFF, dalpha));   /* pad = trim offset */
    return GM_UNDEF_V;
}
GMB(draw_background_part_ext)
{
    (void)self; (void)other;
    const ge_bg_t *b = ge_pack_bg(pack, A_INT(0));
    if (!b || b->asset == NONE) return GM_UNDEF_V;
    int l = A_INT(1), t = A_INT(2), w = A_INT(3), h = A_INT(4);
    float dx = A_REAL(5), dy = A_REAL(6);
    int bo_x = (int)(b->pad & 0xFFFF), bo_y = (int)(b->pad >> 16);           /* trimmed texture: part rect is in the untrimmed image */
    if (bo_x || bo_y) {
        if (l < bo_x) { dx += (bo_x - l) * A_REAL(7); w -= bo_x - l; l = bo_x; }
        if (t < bo_y) { dy += (bo_y - t) * A_REAL(8); h -= bo_y - t; t = bo_y; }
        l -= bo_x; t -= bo_y;
    }
    if (l < 0) { w += l; l = 0; }
    if (t < 0) { h += t; t = 0; }
    if (l + w > b->w) w = b->w - l;
    if (t + h > b->h) h = b->h - t;
    if (w <= 0 || h <= 0) return GM_UNDEF_V;
    ge_gfx_draw_xf(ge_pack_tex(pack, b->asset), l, t, w, h, dx - cam_x, dy - cam_y, 0, 0, A_REAL(7), A_REAL(8), 0, abgr(A_INT(9), A_REAL(10)));
    return GM_UNDEF_V;
}

/* ---------------- text ---------------- */
static unsigned utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = s[0];
    if (c < 0x80) { *p += 1; return c; }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { *p += 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) { *p += 3; return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
    *p += 1;
    return c;
}
/* text line iteration: '\n' and an unescaped '#' end a line; "\#" is a literal '#'; maxw > 0 wraps at spaces */
typedef struct { const ge_font_t *f; } tfont;
static const ge_font_t *cur_font(void) { return ge_pack_font(pack, (int)dfont); }
static int glyph_adv(const ge_font_t *f, unsigned ch)
{
    const ge_glyph_t *g = ge_pack_glyph(pack, f, ch);
    return g ? g->shift : 0;
}
static int line_width(const ge_font_t *f, const char *s, const char *e)
{
    int w = 0;
    while (s < e) {
        unsigned ch = utf8_next(&s);
        if (ch == '\\' && s < e && *s == '#') { ch = '#'; s++; }
        w += glyph_adv(f, ch);
    }
    return w;
}
/* finds the end of the next line starting at s; returns pointer past the separator; *end = end of the line text */
static const char *next_line(const ge_font_t *f, const char *s, const char **end, int maxw)
{
    const char *p = s, *last_space = NULL;
    int w = 0;
    while (*p) {
        const char *q = p;
        unsigned ch = utf8_next(&q);
        if (ch == '\n') { *end = p; return q; }
        if (ch == '#' && !(p > s && p[-1] == '\\')) { *end = p; return q; }
        if (ch == '\r') { p = q; continue; }
        if (ch == ' ') last_space = p;
        int adv = glyph_adv(f, ch);
        if (maxw > 0 && w + adv > maxw && p > s) {
            if (last_space) { *end = last_space; return last_space + 1; }
            *end = p; return p;
        }
        w += adv;
        p = q;
    }
    *end = p;
    return p;
}
static void text_draw(float x, float y, const char *str, float xs, float ys, float rot, int sep, int maxw, uint32_t col)
{
    const ge_font_t *f = cur_font();
    if (!f || f->asset == NONE) return;
    ge_tex_t tex = ge_pack_tex(pack, f->asset);
    int lh = sep > 0 ? sep : f->line_h;
    /* count lines for vertical alignment */
    int nl = 1;
    for (const char *s = str, *e;;) { const char *nx = next_line(f, s, &e, maxw); if (!*nx) break; s = nx; nl++; }
    float vy = valign == 1 ? -nl * lh * 0.5f : (valign == 2 ? -(float)nl * lh : 0);
    int row = 0;
    for (const char *s = str;; row++) {
        const char *e;
        const char *nx = next_line(f, s, &e, maxw);
        float pen = halign == 1 ? -line_width(f, s, e) * 0.5f : (halign == 2 ? -(float)line_width(f, s, e) : 0);
        for (const char *p = s; p < e;) {
            unsigned ch = utf8_next(&p);
            if (ch == '\\' && p < e && *p == '#') { ch = '#'; p++; }
            const ge_glyph_t *g = ge_pack_glyph(pack, f, ch);
            if (!g) continue;
            if (g->w && g->h)
                ge_gfx_draw_xf(tex, g->x, g->y, g->w, g->h, x - cam_x, y - cam_y, pen + g->offset, vy + row * lh, xs, ys, rot, col);
            pen += g->shift;
        }
        if (!*nx) break;
        s = nx;
    }
}
#define STR0(n) char t_##n[256]; const char *s_##n = gm_cstr(GM_ARG(n), t_##n, sizeof t_##n)
GMB(draw_text) { (void)self; (void)other; STR0(2); text_draw(A_REAL(0), A_REAL(1), s_2, 1, 1, 0, -1, 0, abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_text_ext) { (void)self; (void)other; STR0(2); text_draw(A_REAL(0), A_REAL(1), s_2, 1, 1, 0, A_INT(3), A_INT(4), abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_text_transformed) { (void)self; (void)other; STR0(2); text_draw(A_REAL(0), A_REAL(1), s_2, A_REAL(3), A_REAL(4), A_REAL(5), -1, 0, abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_text_transformed_color)
{
    (void)self; (void)other;
    STR0(2);
    text_draw(A_REAL(0), A_REAL(1), s_2, A_REAL(3), A_REAL(4), A_REAL(5), -1, 0, abgr(A_INT(6), A_REAL(10)));
    return GM_UNDEF_V;
}
GMB(draw_text_color) { (void)self; (void)other; STR0(2); text_draw(A_REAL(0), A_REAL(1), s_2, 1, 1, 0, -1, 0, abgr(A_INT(3), A_REAL(7))); return GM_UNDEF_V; }
GMB(string_width)
{
    (void)self; (void)other;
    char t[256]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t);
    const ge_font_t *f = cur_font();
    if (!f) return gm_real(0);
    int best = 0;
    for (const char *e; ; ) { const char *nx = next_line(f, s, &e, 0); int w = line_width(f, s, e); if (w > best) best = w; if (!*nx) break; s = nx; }
    return gm_real(best);
}
GMB(string_height)
{
    (void)self; (void)other;
    char t[256]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t);
    const ge_font_t *f = cur_font();
    if (!f) return gm_real(0);
    int n = 1;
    for (const char *e; ; n++) { const char *nx = next_line(f, s, &e, 0); if (!*nx) break; s = nx; }
    return gm_real(n * f->line_h);
}

/* ---------------- state ---------------- */
GMB(draw_set_color) { (void)self; (void)other; dcolor = (uint32_t)A_INT(0) & 0xFFFFFF; return GM_UNDEF_V; }
GMB(draw_set_alpha) { (void)self; (void)other; dalpha = (float)A_REAL(0); return GM_UNDEF_V; }
GMB(draw_set_font) { (void)self; (void)other; dfont = (uint32_t)A_INT(0); return GM_UNDEF_V; }
GMB(draw_set_halign) { (void)self; (void)other; halign = A_INT(0); return GM_UNDEF_V; }
GMB(draw_set_valign) { (void)self; (void)other; valign = A_INT(0); return GM_UNDEF_V; }

/* ---------------- primitives ---------------- */
static void quad(float x0, float y0, float x1, float y1, uint32_t col)
{
    float x[4] = {x0 - cam_x, x1 - cam_x, x0 - cam_x, x1 - cam_x}, y[4] = {y0 - cam_y, y0 - cam_y, y1 - cam_y, y1 - cam_y};
    ge_gfx_flat(0, x, y, 4, col);
}
static void line_w(float x0, float y0, float x1, float y1, float w, uint32_t col)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.0001f) { quad(x0 - w * 0.5f, y0 - w * 0.5f, x0 + w * 0.5f, y0 + w * 0.5f, col); return; }
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    float x[4] = {x0 + nx - cam_x, x0 - nx - cam_x, x1 + nx - cam_x, x1 - nx - cam_x}, y[4] = {y0 + ny - cam_y, y0 - ny - cam_y, y1 + ny - cam_y, y1 - ny - cam_y};
    ge_gfx_flat(0, x, y, 4, col);
}
static void rect(float x1, float y1, float x2, float y2, int outline, uint32_t col)
{
    if (x2 < x1) { float t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { float t = y1; y1 = y2; y2 = t; }
    if (!outline) { quad(x1, y1, x2 + 1, y2 + 1, col); return; }
    quad(x1, y1, x2 + 1, y1 + 1, col); quad(x1, y2, x2 + 1, y2 + 1, col);
    quad(x1, y1 + 1, x1 + 1, y2, col); quad(x2, y1 + 1, x2 + 1, y2, col);
}
GMB(draw_rectangle) { (void)self; (void)other; rect(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), A_INT(4), abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_rectangle_color) { (void)self; (void)other; rect(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), A_INT(8), abgr(A_INT(4), dalpha)); return GM_UNDEF_V; }
GMB(draw_roundrect) { (void)self; (void)other; rect(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), A_INT(4), abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_line) { (void)self; (void)other; line_w(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), 1, abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_line_width) { (void)self; (void)other; line_w(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), A_REAL(4), abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_line_color) { (void)self; (void)other; line_w(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), 1, abgr(A_INT(4), dalpha)); return GM_UNDEF_V; }
GMB(draw_line_width_color) { (void)self; (void)other; line_w(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), A_REAL(4), abgr(A_INT(5), dalpha)); return GM_UNDEF_V; }
GMB(draw_point_color) { (void)self; (void)other; quad(A_REAL(0), A_REAL(1), A_REAL(0) + 1, A_REAL(1) + 1, abgr(A_INT(2), dalpha)); return GM_UNDEF_V; }
static void ellipse(float cx, float cy, float rx, float ry, int outline, uint32_t col)
{
    int n = (int)(rx > ry ? rx : ry) + 8;
    if (n > 48) n = 48;
    if (n < 12) n = 12;
    float x[64], y[64];
    static float tc[49][50], ts[49][50]; static unsigned char tdone[49];      /* unit-circle tables per segment count: no libm trig per frame */
    if (!tdone[n]) { for (int i = 0; i <= n; i++) { float a = 2 * PI_F * i / n; tc[n][i] = cosf(a); ts[n][i] = sinf(a); } tdone[n] = 1; }
    if (!outline) {
        x[0] = cx - cam_x; y[0] = cy - cam_y;
        for (int i = 0; i <= n; i++) { x[i + 1] = cx + tc[n][i] * rx - cam_x; y[i + 1] = cy - ts[n][i] * ry - cam_y; }
        ge_gfx_flat(1, x, y, n + 2, col);
    } else {
        for (int i = 0; i < n; i++) {
            line_w(cx + tc[n][i] * rx, cy - ts[n][i] * ry, cx + tc[n][i + 1] * rx, cy - ts[n][i + 1] * ry, 1, col);
        }
    }
}
extern unsigned (*gm_prof_cyc)(void); extern void gm_prof_fn(int, unsigned);
GMB(draw_circle) { (void)self; (void)other; unsigned c0 = gm_prof_cyc ? gm_prof_cyc() : 0; ellipse(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(2), A_INT(3), abgr(dcolor, dalpha)); if (gm_prof_cyc) gm_prof_fn(0, gm_prof_cyc() - c0); return GM_UNDEF_V; }
GMB(draw_ellipse_color)
{
    (void)self; (void)other;
    float x1 = A_REAL(0), y1 = A_REAL(1), x2 = A_REAL(2), y2 = A_REAL(3);
    ellipse((x1 + x2) * 0.5f, (y1 + y2) * 0.5f, fabsf(x2 - x1) * 0.5f, fabsf(y2 - y1) * 0.5f, A_INT(6), abgr(A_INT(4), dalpha));
    return GM_UNDEF_V;
}
static void tri(float x1, float y1, float x2, float y2, float x3, float y3, int outline, uint32_t col)
{
    if (outline) { line_w(x1, y1, x2, y2, 1, col); line_w(x2, y2, x3, y3, 1, col); line_w(x3, y3, x1, y1, 1, col); return; }
    float x[3] = {x1 - cam_x, x2 - cam_x, x3 - cam_x}, y[3] = {y1 - cam_y, y2 - cam_y, y3 - cam_y};
    ge_gfx_flat(0, x, y, 3, col);
}
GMB(draw_triangle) { (void)self; (void)other; tri(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), A_REAL(4), A_REAL(5), A_INT(6), abgr(dcolor, dalpha)); return GM_UNDEF_V; }
GMB(draw_triangle_color) { (void)self; (void)other; tri(A_REAL(0), A_REAL(1), A_REAL(2), A_REAL(3), A_REAL(4), A_REAL(5), A_INT(12), abgr(A_INT(6), dalpha)); return GM_UNDEF_V; }

static void h_draw_self(gm_inst *i) { gmb_draw_self(i, NULL, 0, NULL); }

void gml_draw_init(ge_pack_t *p)
{
    pack = p;
    gm_hk.room_start = h_room_start;
    gm_hk.room_end = h_room_end;
    gm_hk.draw_begin = h_draw_begin;
    gm_hk.draw_end = h_draw_end;
    gm_hk.draw_layers = h_draw_layers;
    gm_hk.draw_self = h_draw_self;
}

/* on-screen debug text in screen-space game px (smallest ASCII font of the pack, white with a black shadow); call before the frame ends */
void gml_draw_debug(int x, int y, const char *str)
{
    static int dbgfont = -2;
    if (dbgfont == -2) {
        dbgfont = -1;
        for (int i = 0, n = ge_pack_font_count(pack); i < n; i++) {
            const ge_font_t *f = ge_pack_font(pack, i);
            if (!f || f->asset == NONE || !ge_pack_glyph(pack, f, 'A') || !ge_pack_glyph(pack, f, '0')) continue;
            if (dbgfont < 0 || f->line_h < ge_pack_font(pack, dbgfont)->line_h) dbgfont = i;
        }
    }
    if (dbgfont < 0) return;
    int scx = cam_x, scy = cam_y, sh = halign, sv = valign;
    uint32_t sf = dfont;
    cam_x = cam_y = 0; halign = valign = 0; dfont = (uint32_t)dbgfont;
    text_draw((float)x + 1, (float)y + 1, str, 1, 1, 0, -1, 0, 0xFF000000u);
    text_draw((float)x, (float)y, str, 1, 1, 0, -1, 0, 0xFFFFFFFFu);
    cam_x = scx; cam_y = scy; halign = sh; valign = sv; dfont = sf;
}
