#include "ge/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ge/room.h"
#include "ge/io.h"

typedef struct { char magic[4]; uint32_t w, h, bg, na, ni, off_a, off_i, off_d, total, view_w, view_h, ninst, off_inst, nspr, off_spr, off_frm, room_speed; } hdr_t;
typedef struct { uint32_t kind, psm_id, wh, stride, clut_len, data_len, off, pad; } ass_t;
typedef struct { int16_t ax, ay, rx, ry; uint16_t sx, sy, w, h, asset, flags; float scx, scy, rot; uint32_t color; int32_t key, aux; } item_t;
typedef struct { uint32_t first; uint16_t nf, pad; float step; } spr_t;        /* frame records, image steps per game step at image_speed 1 */
typedef struct { uint16_t asset, w, h; int16_t rx, ry; uint16_t pad; } frm_t;
enum { F_TX = 1, F_TY = 2, F_BG = 4, F_GRID = 8, F_INST = 16, F_BGSLOT = 32, F_BGOFF = 64 };   /* BGSLOT: background layer of room slot aux; BGOFF: hidden at room start */
enum { T_MIRROR = 1u << 28, T_FLIP = 1u << 29, T_ROTATE = 1u << 30, T_IDX = 0x7FFFF };
typedef struct { char magic[4]; uint32_t ver, n, index_off, index_len, na, aindex_off, aindex_len, data_off, total;
                 uint32_t spr_off, n_spr, frm_off, n_frm, bg_off, n_bg, font_off, n_font, glyph_off, n_glyph; } phdr_t;     /* v4 tables from spr_off on */
typedef struct { char name[32]; uint32_t off, len, w, h; } pent_t;

typedef struct { ge_pack_t *p; uint32_t id; } ldr_t;
struct ge_pack { ge_file_t *f; phdr_t h; pent_t *e; ass_t *a; uint8_t *stage; ldr_t *ld;
                 ge_sprite_t *spr; ge_frame_t *frm; ge_bg_t *bg; ge_font_t *font; ge_glyph_t *glyph;
                 int *tcache; int *tused; int ntused; };
struct ge_room { uint8_t *file; const hdr_t *h; const item_t *items; ge_tex_t *tex; const uint32_t *ids;
                 const spr_t *spr; const frm_t *frm; ge_inst_t *inst; int *key, *inst_item; uint32_t *order; int dirty; };

ge_pack_t *ge_pack_open(const char *name)
{
    ge_file_t *f = ge_io_open(name);
    if (!f) return NULL;
    ge_pack_t *p = calloc(1, sizeof *p);
    uint8_t *tmp = ge_io_alloc(2048);
    if (ge_io_pread(f, 0, tmp, 2048) < 0) goto fail;
    memcpy(&p->h, tmp, sizeof p->h);
    ge_io_free(tmp);
    if (memcmp(p->h.magic, "GMPK", 4) || (p->h.ver != 3 && p->h.ver != 4)) goto fail2;
    p->e = ge_io_alloc(p->h.index_len);
    p->a = ge_io_alloc(p->h.aindex_len);
    if (ge_io_pread(f, p->h.index_off, p->e, p->h.index_len) < 0 || ge_io_pread(f, p->h.aindex_off, p->a, p->h.aindex_len) < 0) goto fail3;
    uint32_t mx = 0;
    for (uint32_t i = 0; i < p->h.na; i++) { uint32_t n = ((p->a[i].clut_len + 15) & ~15u) + p->a[i].data_len; if (n > mx) mx = n; }
    p->stage = ge_io_alloc(mx);
    p->ld = malloc(sizeof(ldr_t) * (p->h.na ? p->h.na : 1));
    for (uint32_t i = 0; i < p->h.na; i++) { p->ld[i].p = p; p->ld[i].id = i; }
    p->f = f;
    if (p->h.ver >= 4) {
        #define LOADT(dst, off, cnt, sz) do { if (cnt) { dst = ge_io_alloc((cnt) * (sz)); if (ge_io_pread(f, off, dst, (cnt) * (sz)) < 0) goto fail3; } } while (0)
        LOADT(p->spr, p->h.spr_off, p->h.n_spr, sizeof(ge_sprite_t));
        LOADT(p->frm, p->h.frm_off, p->h.n_frm, sizeof(ge_frame_t));
        LOADT(p->bg, p->h.bg_off, p->h.n_bg, sizeof(ge_bg_t));
        LOADT(p->font, p->h.font_off, p->h.n_font, sizeof(ge_font_t));
        LOADT(p->glyph, p->h.glyph_off, p->h.n_glyph, sizeof(ge_glyph_t));
        #undef LOADT
    }
    p->tcache = calloc(p->h.na ? p->h.na : 1, sizeof(int));
    p->tused = malloc(sizeof(int) * (p->h.na ? p->h.na : 1));
    return p;
fail:  ge_io_free(tmp);
fail2: ge_io_close(f); free(p);
    return NULL;
fail3: ge_io_free(p->e); ge_io_free(p->a); goto fail2;
}
int ge_pack_room_count(const ge_pack_t *p) { return p->h.n; }
const char *ge_pack_room_name(const ge_pack_t *p, int i) { return p->e[i].name; }
int ge_pack_find(const ge_pack_t *p, const char *room)
{
    for (uint32_t i = 0; i < p->h.n; i++) if (!strcmp(p->e[i].name, room)) return i;
    return -1;
}


int ge_pack_sprite_count(const ge_pack_t *p) { return p->h.n_spr; }
const ge_sprite_t *ge_pack_sprite(const ge_pack_t *p, int i) { return (i >= 0 && (uint32_t)i < p->h.n_spr) ? &p->spr[i] : NULL; }
const ge_frame_t *ge_pack_frame(const ge_pack_t *p, const ge_sprite_t *s, int f) { return &p->frm[s->first + f]; }
int ge_pack_bg_count(const ge_pack_t *p) { return p->h.n_bg; }
const ge_bg_t *ge_pack_bg(const ge_pack_t *p, int i) { return (i >= 0 && (uint32_t)i < p->h.n_bg) ? &p->bg[i] : NULL; }
int ge_pack_font_count(const ge_pack_t *p) { return p->h.n_font; }
const ge_font_t *ge_pack_font(const ge_pack_t *p, int i) { return (i >= 0 && (uint32_t)i < p->h.n_font) ? &p->font[i] : NULL; }
const ge_glyph_t *ge_pack_glyph(const ge_pack_t *p, const ge_font_t *f, unsigned ch)
{
    int lo = 0, hi = f->nglyph - 1;
    const ge_glyph_t *g = &p->glyph[f->first_glyph];
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (g[m].ch == ch) return &g[m];
        if (g[m].ch < ch) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

/* streaming loader called by the platform gfx module whenever a texture must be uploaded */
static int asset_load(void *user, const void **clut, const void **pixels)
{
    ldr_t *l = user; ge_pack_t *p = l->p; const ass_t *a = &p->a[l->id];
    uint32_t cl = (a->clut_len + 15) & ~15u;
    if (ge_io_pread(p->f, a->off, p->stage, cl + a->data_len) < 0) return -1;
    *clut = p->stage; *pixels = p->stage + cl;
    return 0;
}

static ge_room_t *room_from_blob(ge_pack_t *p, uint8_t *f, int sz)
{
    if (sz < 64 || memcmp(f, "GMR4", 4)) { ge_io_free(f); return NULL; }
    ge_room_t *r = calloc(1, sizeof *r);
    r->file = f; r->h = (const hdr_t *)f;
    ge_gfx_set_view(r->h->view_w, r->h->view_h); r->items = (const item_t *)(f + r->h->off_i);
    uint32_t na = r->h->na;
    r->tex = malloc(sizeof(ge_tex_t) * (na ? na : 1));
    r->ids = (const uint32_t *)(f + r->h->off_a);
    for (uint32_t i = 0; i < na; i++) {
        const ass_t *a = &p->a[r->ids[i]];
        ge_texdesc_t d = {
            .fmt = (a->psm_id & 255) ? GE_FMT_I8 : GE_FMT_I4,
            .w = a->wh & 0xffff, .h = a->wh >> 16, .stride = a->stride,
            .clut_len = a->clut_len, .pixels_len = a->data_len,
            .load = asset_load, .user = &p->ld[r->ids[i]], .key = r->ids[i] + 1 };
        r->tex[i] = ge_gfx_tex_create(&d);
    }
    r->spr = (const spr_t *)(f + r->h->off_spr); r->frm = (const frm_t *)(f + r->h->off_frm);
    uint32_t ni = r->h->ni, nin = r->h->ninst;
    r->inst = malloc(sizeof(ge_inst_t) * (nin ? nin : 1)); memcpy(r->inst, f + r->h->off_inst, sizeof(ge_inst_t) * nin);
    r->key = malloc(sizeof(int) * (ni ? ni : 1)); r->order = malloc(sizeof(uint32_t) * (ni ? ni : 1)); r->inst_item = malloc(sizeof(int) * (nin ? nin : 1));
    for (uint32_t i = 0; i < ni; i++) {
        r->key[i] = r->items[i].key; r->order[i] = i;
        if (r->items[i].flags & F_INST) r->inst_item[r->items[i].aux] = i;
    }
    return r;
}

ge_room_t *ge_room_load_index(ge_pack_t *p, int i)
{
    if (i < 0 || (uint32_t)i >= p->h.n) return NULL;
    uint8_t *b = ge_io_alloc(p->e[i].len);
    if (ge_io_pread(p->f, p->e[i].off, b, p->e[i].len) < 0) { ge_io_free(b); return NULL; }
    return room_from_blob(p, b, p->e[i].len);
}

void ge_room_free(ge_room_t *r)
{
    if (!r) return;
    for (uint32_t i = 0; i < r->h->na; i++) ge_gfx_tex_release(r->tex[i]);
    ge_io_free(r->file); free(r->tex); free(r->inst); free(r->key); free(r->order); free(r->inst_item); free(r);
}

static void draw_grid(const ge_room_t *r, const item_t *it, int cx, int cy)
{
    int tx = it->sx, ty = it->sy, w = it->w, h = it->h, bx = it->rx, by = it->ry, cols = (int)it->scx, rpb = (int)it->scy, cpb = (int)it->rot;
    int nbc = cpb ? (cols + cpb - 1) / cpb : 1;
    const uint32_t *tiles = (const uint32_t *)(r->file + it->color);
    int vw, vh; ge_gfx_get_view(&vw, &vh);
    int x0 = (int)floorf((cx - it->ax) / (float)w), x1 = (int)ceilf((cx + vw - it->ax) / (float)w);
    int y0 = (int)floorf((cy - it->ay) / (float)h), y1 = (int)ceilf((cy + vh - it->ay) / (float)h);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > tx) x1 = tx; if (y1 > ty) y1 = ty;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        uint32_t v = tiles[y * tx + x];
        if (!v) continue;
        int idx = v & T_IDX, col = idx % cols, row = idx / cols;
        int band = (rpb ? row / rpb : 0) * nbc + (cpb ? col / cpb : 0), rr = rpb ? row % rpb : row, cc = cpb ? col % cpb : col;
        int sx = bx + cc * (w + 2 * bx), sy = by + rr * (h + 2 * by);
        float ax = it->ax + x * w - cx, ay = it->ay + y * h - cy;
        ge_tex_t t = r->tex[it->asset + band];
        if (v & (T_MIRROR | T_FLIP | T_ROTATE))
            ge_gfx_draw_xf(t, sx, sy, w, h, ax + w * 0.5f, ay + h * 0.5f, -w * 0.5f, -h * 0.5f, (v & T_MIRROR) ? -1.f : 1.f,
                           (v & T_FLIP) ? -1.f : 1.f, (v & T_ROTATE) ? -90.f : 0.f, 0xFFFFFFFFu);
        else
            ge_gfx_draw_xf(t, sx, sy, w, h, ax, ay, 0, 0, 1, 1, 0, 0xFFFFFFFFu);
    }
}

static void draw_item(const ge_room_t *r, const item_t *it, float ax, float ay)
{
    ge_gfx_draw_xf(r->tex[it->asset], it->sx, it->sy, it->w, it->h, ax, ay, it->rx, it->ry, it->scx, it->scy, it->rot, it->color);
}

static int frame_of(const ge_room_t *r, const ge_inst_t *in, const spr_t **sp)
{
    *sp = &r->spr[in->sprite];
    int n = (*sp)->nf; if (n <= 0) return -1;
    int f = (int)floorf(in->image_index) % n;
    return f < 0 ? f + n : f;
}
static void draw_inst(const ge_room_t *r, const ge_inst_t *in, int cx, int cy)
{
    const spr_t *sp; int f = frame_of(r, in, &sp);
    if (!in->visible || f < 0) return;
    const frm_t *fr = &r->frm[sp->first + f];
    if (!fr->w) return;
    ge_gfx_draw_xf(r->tex[fr->asset], 0, 0, fr->w, fr->h, in->x - cx, in->y - cy, fr->rx, fr->ry, in->xscale, in->yscale, in->angle, in->color);
}
/* stable insertion sort of the draw order by key (ascending = drawn first); cheap because the order is nearly sorted */
static void resort(ge_room_t *r)
{
    for (uint32_t i = 1; i < r->h->ni; i++) {
        uint32_t v = r->order[i]; int k = r->key[v]; int j = (int)i - 1;
        while (j >= 0 && r->key[r->order[j]] > k) { r->order[j + 1] = r->order[j]; j--; }
        r->order[j + 1] = v;
    }
    r->dirty = 0;
}
int ge_room_speed(const ge_room_t *r) { return r->h->room_speed; }
int ge_room_inst_count(const ge_room_t *r) { return r->h->ninst; }
ge_inst_t *ge_room_inst(ge_room_t *r, int i) { return &r->inst[i]; }
void ge_room_inst_set_depth(ge_room_t *r, int i, int depth) { r->key[r->inst_item[i]] = -depth; r->dirty = 1; }
void ge_room_step(ge_room_t *r)
{
    for (uint32_t i = 0; i < r->h->ninst; i++) {
        ge_inst_t *in = &r->inst[i];
        const spr_t *sp = &r->spr[in->sprite];
        if (sp->nf < 2 || in->image_speed == 0) continue;
        in->image_index += in->image_speed * sp->step;
        float n = (float)sp->nf;
        if (in->image_index >= n || in->image_index < 0) in->image_index -= n * floorf(in->image_index / n);
    }
}

void ge_room_draw(const ge_room_t *r, int cx, int cy)
{
    ge_gfx_frame_begin(r->h->bg);
    float RW = (float)r->h->w, RH = (float)r->h->h;
    if (r->dirty) resort((ge_room_t *)r);
    for (uint32_t oi = 0; oi < r->h->ni; oi++) {
        const item_t *it = &r->items[r->order[oi]];
        if (it->flags & F_INST) { draw_inst(r, &r->inst[it->aux], cx, cy); continue; }
        if (it->flags & F_GRID) { draw_grid(r, it, cx, cy); continue; }
        if (it->flags & F_BGOFF) continue;
        if (it->flags & F_BG) {                       /* background layer, optionally repeated over the room */
            float w = it->w * it->scx, h = it->h * it->scy;
            float x0 = it->ax, y0 = it->ay;
            if (it->flags & F_TX) x0 = it->ax - ceilf(it->ax / w) * w;
            if (it->flags & F_TY) y0 = it->ay - ceilf(it->ay / h) * h;
            int tx = it->flags & F_TX, ty = it->flags & F_TY;
            for (float y = y0; y < RH; y += h) {
                for (float x = x0; x < RW; x += w) {
                    if (x + w > 0 && y + h > 0) draw_item(r, it, x - cx, y - cy);
                    if (!tx) break;
                }
                if (!ty) break;
            }
        } else draw_item(r, it, it->ax - cx, it->ay - cy);
    }
    ge_gfx_frame_end();
}
void ge_room_size(const ge_room_t *r, int *w, int *h) { *w = r->h->w; *h = r->h->h; }
void ge_room_stats(const ge_room_t *r, int *a, int *i) { *a = r->h->na; *i = r->h->ni; }
int ge_room_missing_tex(const ge_room_t *r) { int n = 0; for (uint32_t i = 0; i < r->h->na; i++) n += r->tex[i] < 0; return n; }

/* upload the textures the first frame at camera (cx,cy) will draw (only what is not already cached in VRAM) */
void ge_room_prefetch(const ge_room_t *r, int cx, int cy)
{
    int vw, vh; ge_gfx_get_view(&vw, &vh);
    uint8_t *seen = calloc(r->h->na ? r->h->na : 1, 1);
    for (uint32_t i = 0; i < r->h->ni; i++) {
        const item_t *it = &r->items[i];
        if (it->flags & F_GRID) {
            int tx = it->sx, ty = it->sy, w = it->w, h = it->h, bx = it->rx, by = it->ry, cols = (int)it->scx, rpb = (int)it->scy, cpb = (int)it->rot;
            int nbc = cpb ? (cols + cpb - 1) / cpb : 1;
            const uint32_t *tiles = (const uint32_t *)(r->file + it->color);
            int x0 = (int)floorf((cx - it->ax) / (float)w), x1 = (int)ceilf((cx + vw - it->ax) / (float)w);
            int y0 = (int)floorf((cy - it->ay) / (float)h), y1 = (int)ceilf((cy + vh - it->ay) / (float)h);
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > tx) x1 = tx; if (y1 > ty) y1 = ty;
            (void)bx; (void)by;
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
                uint32_t v = tiles[y * tx + x];
                if (!v) continue;
                int idx = v & T_IDX, col = idx % cols, row = idx / cols;
                int a = it->asset + (rpb ? row / rpb : 0) * nbc + (cpb ? col / cpb : 0);
                if (!seen[a]) { seen[a] = 1; ge_gfx_tex_prefetch(r->tex[a]); }
            }
            continue;
        }
        if (it->flags & F_INST) {
            const ge_inst_t *in = &r->inst[it->aux]; const spr_t *sp = &r->spr[in->sprite];
            if (!in->visible || in->x < cx - 128 || in->x > cx + vw + 128 || in->y < cy - 128 || in->y > cy + vh + 128) continue;
            for (int f = 0; f < sp->nf; f++) {
                const frm_t *fr = &r->frm[sp->first + f];
                if (fr->w && !seen[fr->asset]) { seen[fr->asset] = 1; ge_gfx_tex_prefetch(r->tex[fr->asset]); }
            }
            continue;
        }
        float ext = (it->w > it->h ? it->w : it->h) * (fabsf(it->scx) > fabsf(it->scy) ? fabsf(it->scx) : fabsf(it->scy)) * 1.42f;
        if (!(it->flags & F_BG) && (it->ax + ext < cx || it->ax - ext > cx + vw || it->ay + ext < cy || it->ay - ext > cy + vh)) continue;
        if (!seen[it->asset]) { seen[it->asset] = 1; ge_gfx_tex_prefetch(r->tex[it->asset]); }
    }
    free(seen);
}

/* ---- dynamic drawing: texture handle for a global asset, valid until ge_pack_frame_end ---- */
ge_tex_t ge_pack_tex(ge_pack_t *p, uint32_t asset)
{
    if (asset >= p->h.na) return -1;
    if (p->tcache[asset]) return p->tcache[asset] - 2;           /* stored +2 so that -1 (failed) is distinguishable from empty */
    const ass_t *a = &p->a[asset];
    ge_texdesc_t d = {
        .fmt = (a->psm_id & 255) ? GE_FMT_I8 : GE_FMT_I4,
        .w = a->wh & 0xffff, .h = a->wh >> 16, .stride = a->stride,
        .clut_len = a->clut_len, .pixels_len = a->data_len,
        .load = asset_load, .user = &p->ld[asset], .key = asset + 1 };
    ge_tex_t t = ge_gfx_tex_create(&d);
    p->tcache[asset] = t + 2;
    p->tused[p->ntused++] = asset;
    return t;
}
void ge_pack_frame_end(ge_pack_t *p)
{
    for (int i = 0; i < p->ntused; i++) {
        int a = p->tused[i];
        if (p->tcache[a] - 2 >= 0) ge_gfx_tex_release(p->tcache[a] - 2);
        p->tcache[a] = 0;
    }
    p->ntused = 0;
}

/* ---- room layers for the GML runtime (instances are dynamic there: F_INST items are skipped) ---- */
int ge_room_item_count(const ge_room_t *r) { return r->h->ni; }
int ge_room_item_key(const ge_room_t *r, int oi)
{
    if (r->dirty) resort((ge_room_t *)r);
    return r->key[r->order[oi]];
}
uint32_t ge_room_bg_color(const ge_room_t *r) { return r->h->bg; }
int ge_room_item_bgslot(const ge_room_t *r, int oi, int *initially_on)
{
    if (r->dirty) resort((ge_room_t *)r);
    const item_t *it = &r->items[r->order[oi]];
    if (!(it->flags & F_BGSLOT)) return -1;
    if (initially_on) *initially_on = !(it->flags & F_BGOFF);
    return it->aux;
}
void ge_room_draw_item(const ge_room_t *r, int oi, int cx, int cy)
{
    const item_t *it = &r->items[r->order[oi]];
    float RW = (float)r->h->w, RH = (float)r->h->h;
    if (it->flags & F_INST) return;
    if (it->flags & F_GRID) { draw_grid(r, it, cx, cy); return; }
    if (it->flags & F_BG) {
        float w = it->w * it->scx, h = it->h * it->scy;
        float x0 = it->ax, y0 = it->ay;
        if (it->flags & F_TX) x0 = it->ax - ceilf(it->ax / w) * w;
        if (it->flags & F_TY) y0 = it->ay - ceilf(it->ay / h) * h;
        int tx = it->flags & F_TX, ty = it->flags & F_TY;
        int vw, vh; ge_gfx_get_view(&vw, &vh);
        for (float y = y0; y < RH; y += h) {
            for (float x = x0; x < RW; x += w) {
                if (x + w > 0 && y + h > 0 && x - cx < vw && y - cy < vh) draw_item(r, it, x - cx, y - cy);
                if (!tx) break;
            }
            if (!ty) break;
        }
    } else draw_item(r, it, it->ax - cx, it->ay - cy);
}
