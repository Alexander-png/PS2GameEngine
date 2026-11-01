/* gml_coll.c - sprite metrics and bounding-box collisions (precise masks come later). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gml/gml_rt.h"

gm_inst **gm_inst_list(int *n);

int gm_inst_bbox(const gm_inst *i, gm_real_t *l, gm_real_t *t, gm_real_t *r, gm_real_t *b)
{
    int s = i->mask >= 0 ? i->mask : i->sprite;
    if (s < 0 || s >= gm_nsprites) return 0;
    const gm_spritedef *d = &gm_sprites[s];
    /* the sprite's bbox is in whole pixels with an inclusive right/bottom; scaling acts on the pixel extents [left, right+1) */
    gm_real_t x0 = i->x + (d->left - d->ox) * i->image_xscale, x1 = i->x + (d->right + 1 - d->ox) * i->image_xscale;
    gm_real_t y0 = i->y + (d->top - d->oy) * i->image_yscale, y1 = i->y + (d->bottom + 1 - d->oy) * i->image_yscale;
    *l = x0 < x1 ? x0 : x1; *r = (x0 < x1 ? x1 : x0) - 1;
    *t = y0 < y1 ? y0 : y1; *b = (y0 < y1 ? y1 : y0) - 1;
    return 1;
}

/* precise collision: does any solid pixel of m's mask fall inside the room rectangle [l..r] x [t..b]? (sprites without a mask: the bounding box counts) */
static int mask_hit(const gm_inst *m, gm_real_t l, gm_real_t t, gm_real_t r, gm_real_t b)
{
    int s = m->mask >= 0 ? m->mask : m->sprite;
    if (s < 0 || s >= gm_nsprites || !gm_sprites[s].mask) return 1;
    const gm_spritedef *d = &gm_sprites[s];
    gm_real_t xs = m->image_xscale ? m->image_xscale : 1, ys = m->image_yscale ? m->image_yscale : 1, bl, bt, br, bb;
    if (gm_inst_bbox(m, &bl, &bt, &br, &bb)) { if (l < bl) l = bl; if (t < bt) t = bt; if (r > br) r = br; if (b > bb) b = bb; }
    int stride = (d->w + 7) / 8;
    for (int y = (int)floor(t); y <= (int)floor(b); y++)
        for (int x = (int)floor(l); x <= (int)floor(r); x++) {
            int px = (int)floor((x + 0.5 - m->x) / xs + d->ox), py = (int)floor((y + 0.5 - m->y) / ys + d->oy);
            if (px < 0 || px >= d->w || py < 0 || py >= d->h) continue;
            if ((d->mask[py * stride + (px >> 3)] >> (7 - (px & 7))) & 1) return 1;
        }
    return 0;
}

static int match(const gm_inst *o, int who)
{
    if (o->dead) return 0;
    if (who == -3) return 1;
    if (who >= 100000) return o->id == who;
    /* inheritance test memoized per target object (hot in per-frame collision_* loops) */
    static int c_who = -2; static unsigned char *c_tab; static int c_n;
    if (o->obj < 0 || o->obj >= gm_nobjs) return 0;
    if (!c_tab || c_n != gm_nobjs) { free(c_tab); c_tab = calloc(gm_nobjs, 1); c_n = gm_nobjs; c_who = -2; }
    if (c_who != who) { memset(c_tab, 0, c_n); c_who = who; }
    if (!c_tab[o->obj]) c_tab[o->obj] = gm_obj_isa(o->obj, who) ? 2 : 1;
    return c_tab[o->obj] == 2;
}

extern unsigned inst_epoch;
/* per-target candidate lists: instances of one object are few, the whole list is not (and walking it chases cold pointers) */
typedef struct { int who; unsigned epoch; int n, cap, alloc; gm_inst **v; } mlist_t;
static mlist_t ml[4] = {{-9}, {-9}, {-9}, {-9}}; static int ml_next;
static mlist_t *mlist_for(int who, gm_inst **L, int n)
{
    for (int k = 0; k < 4; k++) if (ml[k].who == who && ml[k].v && ml[k].epoch == inst_epoch && ml[k].cap >= 0) {
        if (ml[k].cap == n) return &ml[k];                        /* cap doubles as the instance count the list was built for */
    }
    mlist_t *m = NULL;
    for (int k = 0; k < 4; k++) if (ml[k].who == who) m = &ml[k];
    if (!m) { m = &ml[ml_next]; ml_next = (ml_next + 1) & 3; }
    if (m->alloc < n) { m->alloc = n + 32; m->v = realloc(m->v, m->alloc * sizeof *m->v); }
    m->who = who; m->epoch = inst_epoch; m->n = 0; m->cap = n;
    for (int k = 0; k < n; k++) if (match(L[k], who)) m->v[m->n++] = L[k];
    return m;
}

gm_inst *gm_collide_rect(gm_inst *self, gm_real_t l, gm_real_t t, gm_real_t r, gm_real_t b, int who, int notme)
{
    int n;
    gm_inst **L = gm_inst_list(&n);
    if (who >= 0 && who < 100000 && n > 24) {
        mlist_t *m = mlist_for(who, L, n);
        for (int k = 0; k < m->n; k++) {
            gm_inst *o = m->v[k];
            gm_real_t ol, ot, orr, ob;
            if (o->dead || (notme && o == self) || !gm_inst_bbox(o, &ol, &ot, &orr, &ob)) continue;
            if (l <= orr && ol <= r && t <= ob && ot <= b && mask_hit(o, l, t, r, b)) return o;
        }
        return NULL;
    }
    for (int k = 0; k < n; k++) {
        gm_inst *o = L[k];
        gm_real_t ol, ot, orr, ob;
        if ((notme && o == self) || !match(o, who) || !gm_inst_bbox(o, &ol, &ot, &orr, &ob)) continue;
        if (l <= orr && ol <= r && t <= ob && ot <= b && mask_hit(o, l, t, r, b)) return o;
    }
    return NULL;
}

gm_inst *gm_place(gm_inst *self, gm_real_t x, gm_real_t y, int who)
{
    gm_real_t l, t, r, b;
    gm_inst tmp = *self;
    tmp.x = x; tmp.y = y;
    if (!gm_inst_bbox(&tmp, &l, &t, &r, &b)) return NULL;
    return gm_collide_rect(self, l, t, r, b, who, 1);
}

/* collision events: for every instance and each Collision handler (own or inherited) */
int gm_collision_events(void)
{
    int n, ran = 0;
    gm_inst **L = gm_inst_list(&n);
    for (int k = 0; k < n; k++) {
        gm_inst *i = L[k];
        gm_real_t l, t, r, b;
        if (i->dead || !gm_inst_bbox(i, &l, &t, &r, &b)) continue;
        int done[64], ndone = 0;                              /* a collision event defined by a child overrides the parent's event for the same target */
        for (int obj = i->obj, g = 0; obj >= 0 && g < 32; obj = gm_objs[obj].parent, g++) {
            const gm_objdef *d = &gm_objs[obj];
            int mark = ndone;
            for (int e = 0; e < d->nevents; e++) {
                if (d->events[e].type != EV_COLLISION) continue;
                int target = d->events[e].sub, ov = 0;
                for (int q = 0; q < mark; q++) if (done[q] == target) ov = 1;
                if (ov) continue;
                if (ndone < 64) done[ndone++] = target;
                for (int m = 0; m < n && !i->dead; m++) {
                    gm_inst *o = L[m];
                    gm_real_t ol, ot, orr, ob;
                    if (o == i || o->dead || !gm_obj_isa(o->obj, target) || !gm_inst_bbox(o, &ol, &ot, &orr, &ob)) continue;
                    if (l <= orr && ol <= r && t <= ob && ot <= b) {
                        gm_real_t il = l > ol ? l : ol, it = t > ot ? t : ot, ir = r < orr ? r : orr, ib = b < ob ? b : ob;
                        if (!mask_hit(i, il, it, ir, ib) || !mask_hit(o, il, it, ir, ib)) continue;
                        if (i->solid || o->solid) {            /* a collision involving a solid instance first puts both back where they were at the start of the step */
                            i->x = i->xprev; i->y = i->yprev; o->x = o->xprev; o->y = o->yprev;
                        }
                        gm_run_event_for(i, obj, EV_COLLISION, target, o);
                        ran++;
                    }
                }
            }
        }
    }
    return ran;
}
