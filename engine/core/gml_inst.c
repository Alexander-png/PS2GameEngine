/* gml_inst.c - GML instances: variable storage, builtin instance variables, objects/events, `with`, the frame loop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "gml/gml_rt.h"

gm_hooks gm_hk;
int gm_room_cur = -1, gm_room_next = -1;
gm_real_t gm_room_speed = 30;
uint64_t gm_frame;

static gm_inst global_store;
gm_inst *gm_global_inst = &global_store;

/* ---------------- variable name ids ---------------- */
static int *name_hash;
static int name_hash_cap;
static unsigned hstr(const char *s) { unsigned h = 2166136261u; while (*s) h = (h ^ (unsigned char)*s++) * 16777619u; return h; }
int gm_vid(const char *name)
{
    if (!name_hash) return -1;
    unsigned h = hstr(name) & (name_hash_cap - 1);
    while (name_hash[h]) {
        int v = name_hash[h] - 1;
        if (!strcmp(gm_var_names[v], name)) return v;
        h = (h + 1) & (name_hash_cap - 1);
    }
    return -1;
}

/* ---------------- variable tables ---------------- */
static void vtab_grow(gm_vtab *t)
{
    int ncap = t->cap ? t->cap * 2 : 8;
    uint16_t *ok = t->key;
    gm_val *ov = t->val;
    int ocap = t->cap;
    t->key = calloc(ncap, sizeof(uint16_t));
    t->val = malloc(ncap * sizeof(gm_val));
    t->cap = ncap;
    t->n = 0;
    for (int i = 0; i < ocap; i++)
        if (ok[i]) {
            unsigned h = (ok[i] * 2654435761u) & (ncap - 1);
            while (t->key[h]) h = (h + 1) & (ncap - 1);
            t->key[h] = ok[i]; t->val[h] = ov[i]; t->n++;
        }
    free(ok); free(ov);
}
int gm_vtab_find(const gm_vtab *t, int vid, gm_val *out)
{
    if (!t->cap) return 0;
    uint16_t k = (uint16_t)(vid + 1);
    unsigned h = (k * 2654435761u) & (t->cap - 1);
    while (t->key[h]) {
        if (t->key[h] == k) { if (out) *out = t->val[h]; return 1; }
        h = (h + 1) & (t->cap - 1);
    }
    return 0;
}
static gm_val *vtab_slot(gm_vtab *t, int vid)
{
    if (t->n * 2 >= t->cap) vtab_grow(t);
    uint16_t k = (uint16_t)(vid + 1);
    unsigned h = (k * 2654435761u) & (t->cap - 1);
    while (t->key[h]) {
        if (t->key[h] == k) return &t->val[h];
        h = (h + 1) & (t->cap - 1);
    }
    t->key[h] = k; t->val[h] = GM_UNDEF_V; t->n++;
    return &t->val[h];
}
void gm_vtab_set(gm_vtab *t, int vid, gm_val v)
{
    if (vid < 0) return;
    gm_val *s = vtab_slot(t, vid);
    gm_val nv = gm_promote(v);
    gm_release(*s);
    *s = nv;
}

/* ---------------- builtin instance variables ---------------- */
enum { K_NONE = 0, K_REAL, K_INT, K_U8, K_SPEED, K_DIR, K_HSPEED, K_VSPEED, K_ID, K_OBJ, K_ALARM, K_BBL, K_BBR, K_BBT, K_BBB, K_SPW, K_SPH, K_SPX, K_SPY, K_IMN };
typedef struct { const char *name; int kind; int off; } bi_t;
static const bi_t BI[] = {
    {"x", K_REAL, offsetof(gm_inst, x)}, {"y", K_REAL, offsetof(gm_inst, y)},
    {"xstart", K_REAL, offsetof(gm_inst, xstart)}, {"ystart", K_REAL, offsetof(gm_inst, ystart)},
    {"path_position", K_REAL, offsetof(gm_inst, path_pos)}, {"path_speed", K_REAL, offsetof(gm_inst, path_spd)},
    {"xprevious", K_REAL, offsetof(gm_inst, xprev)}, {"yprevious", K_REAL, offsetof(gm_inst, yprev)},
    {"hspeed", K_HSPEED, 0}, {"vspeed", K_VSPEED, 0}, {"speed", K_SPEED, 0}, {"direction", K_DIR, 0},
    {"friction", K_REAL, offsetof(gm_inst, friction)}, {"gravity", K_REAL, offsetof(gm_inst, gravity)},
    {"gravity_direction", K_REAL, offsetof(gm_inst, gravity_dir)},
    {"image_index", K_REAL, offsetof(gm_inst, image_index)}, {"image_speed", K_REAL, offsetof(gm_inst, image_speed)},
    {"image_xscale", K_REAL, offsetof(gm_inst, image_xscale)}, {"image_yscale", K_REAL, offsetof(gm_inst, image_yscale)},
    {"image_angle", K_REAL, offsetof(gm_inst, image_angle)}, {"image_alpha", K_REAL, offsetof(gm_inst, image_alpha)},
    {"image_blend", K_INT, offsetof(gm_inst, image_blend)},
    {"depth", K_INT, offsetof(gm_inst, depth)}, {"sprite_index", K_INT, offsetof(gm_inst, sprite)}, {"mask_index", K_INT, offsetof(gm_inst, mask)},
    {"visible", K_U8, offsetof(gm_inst, visible)}, {"solid", K_U8, offsetof(gm_inst, solid)}, {"persistent", K_U8, offsetof(gm_inst, persistent)},
    {"bbox_left", K_BBL, 0}, {"bbox_right", K_BBR, 0}, {"bbox_top", K_BBT, 0}, {"bbox_bottom", K_BBB, 0},
    {"sprite_width", K_SPW, 0}, {"sprite_height", K_SPH, 0}, {"sprite_xoffset", K_SPX, 0}, {"sprite_yoffset", K_SPY, 0}, {"image_number", K_IMN, 0},
    {"id", K_ID, 0}, {"object_index", K_OBJ, 0}, {"alarm", K_ALARM, 0},
};
static unsigned char *bikind;
static short *bioff;
static int vid_alarm = -1;
static unsigned char *isglob;      /* builtin variables that live in the global store whichever instance reads them */

static gm_real_t speed_of(const gm_inst *i)
{
    if (i->hspeed == i->sp_h && i->vspeed == i->sp_v) return i->speed_set;
    return sqrt(i->hspeed * i->hspeed + i->vspeed * i->vspeed);
}
static void set_polar(gm_inst *i, gm_real_t sp, gm_real_t dir)
{
    gm_real_t r = dir * 3.14159265358979323846 / 180.0;
    i->direction = dir;
    i->hspeed = sp * cos(r);
    i->vspeed = -sp * sin(r);
    i->speed_set = sp; i->sp_h = i->hspeed; i->sp_v = i->vspeed;
}
void gm_set_polar(gm_inst *i, gm_real_t sp, gm_real_t dir) { set_polar(i, sp, dir); }
static gm_real_t dir_of(const gm_inst *i)
{
    if (i->hspeed == i->sp_h && i->vspeed == i->sp_v) return i->direction;     /* untouched since set_polar: the stored direction is exact (and right for a negative speed) */
    if (i->hspeed == 0 && i->vspeed == 0) return i->direction;
    gm_real_t d = atan2(-i->vspeed, i->hspeed) * 180.0 / 3.14159265358979323846;
    return d < 0 ? d + 360 : d;
}

static gm_val bi_get(gm_inst *i, int kind, int off)
{
    switch (kind) {
    case K_REAL: return gm_real(*(gm_real_t *)((char *)i + off));
    case K_INT: return gm_real(*(int *)((char *)i + off));
    case K_U8: return gm_real(*((uint8_t *)i + off));
    case K_HSPEED: return gm_real(i->hspeed);
    case K_VSPEED: return gm_real(i->vspeed);
    case K_SPEED: return gm_real(speed_of(i));
    case K_DIR: return gm_real(dir_of(i));
    case K_ID: return gm_real(i->id);
    case K_OBJ: return gm_real(i->obj);
    case K_BBL: case K_BBR: case K_BBT: case K_BBB: {
        gm_real_t l, t, r, b;
        if (!gm_inst_bbox(i, &l, &t, &r, &b)) return gm_real(kind == K_BBL || kind == K_BBR ? i->x : i->y);
        return gm_real(kind == K_BBL ? l : kind == K_BBR ? r : kind == K_BBT ? t : b);
    }
    case K_SPW: case K_SPH: case K_SPX: case K_SPY: case K_IMN: {
        if (i->sprite < 0 || i->sprite >= gm_nsprites) return gm_real(0);
        const gm_spritedef *d = &gm_sprites[i->sprite];
        return gm_real(kind == K_SPW ? d->w * i->image_xscale : kind == K_SPH ? d->h * i->image_yscale :
                       kind == K_SPX ? d->ox * i->image_xscale : kind == K_SPY ? d->oy * i->image_yscale : d->nframes);
    }
    default: return GM_UNDEF_V;
    }
}
static void bi_set(gm_inst *i, int kind, int off, gm_val v)
{
    gm_real_t r = gm_to_real(v);
    switch (kind) {
    case K_REAL: *(gm_real_t *)((char *)i + off) = r; break;
    case K_INT: *(int *)((char *)i + off) = (int)r; break;
    case K_U8: *((uint8_t *)i + off) = r > 0.5; break;
    case K_HSPEED: i->hspeed = r; break;
    case K_VSPEED: i->vspeed = r; break;
    case K_SPEED: set_polar(i, r, dir_of(i)); break;
    case K_DIR: set_polar(i, speed_of(i), r); break;
    default: break;
    }
}

gm_val gm_get(gm_inst *i, int vid)
{
    gm_val v;
    if (!i) return GM_UNDEF_V;
    if (i != gm_global_inst && bikind[vid] && bikind[vid] != K_ALARM) return bi_get(i, bikind[vid], bioff[vid]);
    if (isglob[vid]) i = gm_global_inst;
    if (gm_vtab_find(&i->vars, vid, &v)) return v;
    return GM_UNDEF_V;
}
void gm_set(gm_inst *i, int vid, gm_val v)
{
    if (!i) return;
    if (i != gm_global_inst && bikind[vid] && bikind[vid] != K_ALARM && bikind[vid] != K_ID && bikind[vid] != K_OBJ) { bi_set(i, bikind[vid], bioff[vid], v); return; }
    if (isglob[vid]) i = gm_global_inst;
    gm_vtab_set(&i->vars, vid, v);
}
gm_inst **gm_inst_list(int *n);
void gm_set_obj(int obj, int vid, gm_val v)
{
    int n; gm_inst **L = gm_inst_list(&n);
    for (int k = 0; k < n; k++) if (!L[k]->dead && gm_obj_isa(L[k]->obj, obj)) gm_set(L[k], vid, v);
}
void gm_seta_obj(int obj, int vid, gm_val index, gm_val v)
{
    int n; gm_inst **L = gm_inst_list(&n);
    for (int k = 0; k < n; k++) if (!L[k]->dead && gm_obj_isa(L[k]->obj, obj)) gm_seta(L[k], vid, index, v);
}
gm_val gm_geta(gm_inst *i, int vid, gm_val index)
{
    int idx = gm_to_int(index);
    gm_val v;
    if (!i) return GM_UNDEF_V;
    if (isglob[vid]) i = gm_global_inst;
    if (vid == vid_alarm && i != gm_global_inst) return (idx >= 0 && idx < GM_MAX_ALARMS) ? gm_real(i->alarm[idx]) : GM_UNDEF_V;
    if (gm_vtab_find(&i->vars, vid, &v) && v.t == GM_ARR) return gm_arr_get(v.u.a, idx);
    return GM_UNDEF_V;
}
void gm_seta(gm_inst *i, int vid, gm_val index, gm_val v)
{
    int idx = gm_to_int(index);
    if (!i) return;
    if (isglob[vid]) i = gm_global_inst;
    if (vid == vid_alarm && i != gm_global_inst) { if (idx >= 0 && idx < GM_MAX_ALARMS) i->alarm[idx] = gm_to_int(v); return; }
    gm_val *s = vtab_slot(&i->vars, vid);
    if (s->t != GM_ARR) { gm_release(*s); gm_arr *a = calloc(1, sizeof(gm_arr)); s->t = GM_ARR; s->u.a = a; }
    gm_arr_set(s->u.a, idx, v);
}
gm_val gm_geta_v(gm_val *slot, gm_val index)
{
    return slot->t == GM_ARR ? gm_arr_get(slot->u.a, gm_to_int(index)) : GM_UNDEF_V;
}
void gm_seta_v(gm_val *slot, gm_val index, gm_val v)
{
    if (slot->t != GM_ARR) { slot->t = GM_ARR; slot->u.a = calloc(1, sizeof(gm_arr)); }
    gm_arr_set(slot->u.a, gm_to_int(index), v);
}
gm_val gm_arg_dyn(int argc, const gm_val *argv, gm_val idx)
{
    int n = gm_to_int(idx);
    return (n >= 0 && n < argc) ? argv[n] : GM_UNDEF_V;
}

typedef struct { int type, sub, obj; } evctx_t;
static evctx_t cur_ev = {-1, 0, -1};
/* ---------------- instance pool ---------------- */
static gm_inst **insts;
static int ninst, capinst;
static gm_inst **by_id;
static int cap_by_id;
static int next_id = 100000;

gm_inst *gm_instance_by_id(int id)
{
    id -= 100000;
    if (id < 0 || id >= cap_by_id) return NULL;
    gm_inst *i = by_id[id];
    return (i && !i->dead) ? i : NULL;
}
int gm_obj_isa(int obj, int target)
{
    for (int guard = 0; obj >= 0 && obj < gm_nobjs && guard < 32; guard++) {
        if (obj == target) return 1;
        obj = gm_objs[obj].parent;
    }
    return 0;
}
int gm_instance_count(int obj)
{
    int n = 0;
    for (int k = 0; k < ninst; k++) if (!insts[k]->dead && (obj < 0 || gm_obj_isa(insts[k]->obj, obj))) n++;
    return n;
}
unsigned inst_epoch = 1;                       /* bumped whenever the instance list is compacted, freed from or re-ordered */
static gm_inst *first_c[4096]; static unsigned first_e[4096];
gm_inst *gm_instance_nth(int obj, int n)
{
    if (n == 0 && obj >= 0 && obj < 4096) {              /* "first instance of object" is looked up many times per frame by scripts */
        if (first_e[obj] == inst_epoch && first_c[obj] && !first_c[obj]->dead) return first_c[obj];
        for (int k = 0; k < ninst; k++)
            if (!insts[k]->dead && gm_obj_isa(insts[k]->obj, obj)) { first_c[obj] = insts[k]; first_e[obj] = inst_epoch; return insts[k]; }
        return NULL;
    }
    for (int k = 0; k < ninst; k++)
        if (!insts[k]->dead && (obj < 0 || gm_obj_isa(insts[k]->obj, obj)) && n-- == 0) return insts[k];
    return NULL;
}
extern unsigned (*gm_prof_cyc)(void); extern void gm_prof_fn(int, unsigned);
gm_inst *gm_obj1(int obj) { unsigned c0 = gm_prof_cyc ? gm_prof_cyc() : 0; gm_inst *r = gm_instance_nth(obj, 0); if (gm_prof_cyc) gm_prof_fn(2, gm_prof_cyc() - c0); return r; }

gm_inst *gm_res(gm_val who, gm_inst *self, gm_inst *other)
{
    int v;
    if (who.t != GM_REAL) return NULL;
    v = (int)who.u.r;
    switch (v) {
    case -1: return self;
    case -2: return other;
    case -4: return NULL;
    case -5: return gm_global_inst;
    case -3: return gm_instance_nth(-1, 0);
    default: break;
    }
    if (v >= 100000) return gm_instance_by_id(v);
    return gm_obj1(v);
}

/* ---------------- events ---------------- */

gm_fn gm_find_event_fn(int obj, int type, int sub, int *found_obj)
{
    for (int guard = 0; obj >= 0 && obj < gm_nobjs && guard < 32; guard++) {
        const gm_objdef *o = &gm_objs[obj];
        for (int k = 0; k < o->nevents; k++)
            if (o->events[k].type == type && o->events[k].sub == sub) {
                if (found_obj) *found_obj = obj;
                return o->events[k].fn;
            }
        obj = o->parent;
    }
    return NULL;
}
static void call_event(gm_inst *i, gm_fn fn, int fobj, int type, int sub, gm_inst *other)
{
    evctx_t save = cur_ev;
    cur_ev.type = type; cur_ev.sub = sub; cur_ev.obj = fobj;
    fn(i, other, 0, NULL);
    cur_ev = save;
}
void gm_run_event_other(gm_inst *i, int type, int sub, gm_inst *other)
{
    int fo;
    gm_fn fn = gm_find_event_fn(i->obj, type, sub, &fo);
    if (fn) call_event(i, fn, fo, type, sub, other);
}
void gm_run_event_for(gm_inst *i, int obj, int type, int sub, gm_inst *other)
{
    const gm_objdef *o = &gm_objs[obj];
    for (int k = 0; k < o->nevents; k++)
        if (o->events[k].type == type && o->events[k].sub == sub) { call_event(i, o->events[k].fn, obj, type, sub, other); return; }
}
gm_inst **gm_inst_list(int *n) { *n = ninst; return insts; }
void gm_run_event(gm_inst *i, int type, int sub) { gm_run_event_other(i, type, sub, NULL); }
void gm_event_inherited(gm_inst *i)
{
    if (cur_ev.obj < 0 || cur_ev.obj >= gm_nobjs) return;
    int par = gm_objs[cur_ev.obj].parent, fo;
    gm_fn fn = gm_find_event_fn(par, cur_ev.type, cur_ev.sub, &fo);
    if (fn) call_event(i, fn, fo, cur_ev.type, cur_ev.sub, NULL);
}
int gm_find_object(const char *name)
{
    for (int k = 0; k < gm_nobjs; k++) if (!strcmp(gm_objs[k].name, name)) return k;
    return -1;
}

/* ---------------- create / destroy ---------------- */
static gm_inst *create_id(gm_real_t x, gm_real_t y, int obj, int id, int run_create)
{
    if (obj < 0 || obj >= gm_nobjs) { gm_logf("instance_create: bad object %d", obj); return NULL; }
    const gm_objdef *d = &gm_objs[obj];
    gm_inst *i = calloc(1, sizeof *i);
    i->obj = obj;
    i->x = x; i->y = y; i->xprev = x; i->yprev = y; i->xstart = x; i->ystart = y;
    i->sprite = d->sprite; i->mask = d->mask; i->depth = d->depth;
    i->visible = d->visible; i->solid = d->solid; i->persistent = d->persistent;
    i->image_xscale = i->image_yscale = 1; i->image_alpha = 1; i->image_speed = 1; i->image_blend = 0xFFFFFF;
    i->gravity_dir = 270;
    for (int k = 0; k < GM_MAX_ALARMS; k++) i->alarm[k] = -1;
    i->id = id ? id : next_id++;
    if (i->id - 100000 >= cap_by_id) {
        int nc = cap_by_id ? cap_by_id * 2 : 1024;
        while (nc <= i->id - 100000) nc *= 2;
        by_id = realloc(by_id, nc * sizeof *by_id);
        memset(by_id + cap_by_id, 0, (nc - cap_by_id) * sizeof *by_id);
        cap_by_id = nc;
    }
    by_id[i->id - 100000] = i;
    if (ninst == capinst) { capinst = capinst ? capinst * 2 : 256; insts = realloc(insts, capinst * sizeof *insts); }
    insts[ninst++] = i;
    if (run_create) { gm_run_event(i, EV_CREATE, 0); i->created = 1; }
    return i;
}
gm_inst *gm_instance_create(gm_real_t x, gm_real_t y, int obj)
{
    static const char *tr = (const char *)-1;
    if (tr == (const char *)-1) tr = getenv("GM_TRACE_CREATE");
    if (tr && obj >= 0 && obj < gm_nobjs && !strcmp(tr, gm_objs[obj].name) && gm_frame < 40)
        gm_logf("create %s from %s ev %d/%d", tr, cur_ev.obj >= 0 ? gm_objs[cur_ev.obj].name : "?", cur_ev.type, cur_ev.sub);
    return create_id(x, y, obj, 0, 1);
}
void gm_instance_destroy(gm_inst *i)
{
    if (!i || i->dead) return;
    gm_run_event(i, EV_DESTROY, 0);
    i->dead = 1;
}
static void free_inst(gm_inst *i)
{
    inst_epoch++;
    for (int k = 0; k < i->vars.cap; k++) if (i->vars.key[k]) gm_release(i->vars.val[k]);
    free(i->vars.key); free(i->vars.val);
    if (i->id >= 100000 && i->id - 100000 < cap_by_id) by_id[i->id - 100000] = NULL;
    free(i);
}
static void sweep_dead(void)
{
    int w = 0;
    for (int k = 0; k < ninst; k++) {
        if (insts[k]->dead) free_inst(insts[k]); else insts[w++] = insts[k];
    }
    ninst = w; inst_epoch++;
}

/* ---------------- with ---------------- */
int gm_with_begin(gm_with_t *w, gm_val who, gm_inst **self, gm_inst **other)
{
    w->list = NULL; w->n = 0; w->i = 0;
    w->save_self = *self; w->save_other = *other;
    if (who.t != GM_REAL) return 0;
    int v = (int)who.u.r;
    if (v == -1 || v == -2 || v == -5 || v >= 100000) {
        gm_inst *t = gm_res(who, *self, *other);
        if (!t) return 0;
        w->list = malloc(sizeof(gm_inst *)); w->list[0] = t; w->n = 1;
    } else if (v == -3 || v >= 0) {
        int cnt = gm_instance_count(v == -3 ? -1 : v);
        if (!cnt) return 0;
        w->list = malloc(cnt * sizeof(gm_inst *));
        for (int k = 0; k < ninst && w->n < cnt; k++)
            if (!insts[k]->dead && (v == -3 || gm_obj_isa(insts[k]->obj, v))) w->list[w->n++] = insts[k];
    } else return 0;
    w->save_self = *self; w->save_other = *other;
    *other = *self;
    *self = w->list[0];
    return 1;
}
int gm_with_next(gm_with_t *w, gm_inst **self, gm_inst **other)
{
    while (++w->i < w->n) {
        if (!w->list[w->i]->dead) { *self = w->list[w->i]; return 1; }
    }
    return 0;
}
void gm_with_end(gm_with_t *w, gm_inst **self, gm_inst **other)
{
    free(w->list);
    w->list = NULL;
    *self = w->save_self;
    *other = w->save_other;
}

gm_val gm_callv(gm_val fn, gm_val inst, gm_inst *self, gm_inst *other, int argc, const gm_val *argv)
{
    (void)inst; (void)self; (void)other; (void)argc; (void)argv;
    gm_logf("callv not supported yet");
    return GM_UNDEF_V;
}

/* ---------------- frame loop ---------------- */
static int cmp_depth(const void *a, const void *b)
{
    const gm_inst *x = *(gm_inst *const *)a, *y = *(gm_inst *const *)b;
    if (x->depth != y->depth) return y->depth - x->depth;           /* larger depth first (drawn behind) */
    return x->id - y->id;
}

/* ---------------- keyboard events ---------------- */
static uint64_t kclr[256];                                           /* frame + 1 in which keyboard_clear(vk) was called */
unsigned gm_key_state(int vk)
{
    if (!gm_hk.key_state || vk < 0 || vk > 255) return 0;
    if (kclr[vk] == gm_frame + 1) return 0;
    return gm_hk.key_state(vk);
}
void gm_key_clear(int vk) { if (vk >= 0 && vk <= 255) kclr[vk] = gm_frame + 1; }
static signed char *obj_haskey;                                      /* per object: 1 when it (or an ancestor) has Keyboard / Key Press / Key Release events */
static int key_hit(int type, int sub)
{
    if (sub == 0) {                                                   /* vk_nokey */
        for (int v = 8; v < 256; v++) if (gm_key_state(v) & 1) return 0;
        return type == EV_KEYBOARD;
    }
    unsigned bit = type == EV_KEYBOARD ? 1 : type == EV_KEYPRESS ? 2 : 4;
    if (sub == 1) { for (int v = 8; v < 256; v++) if (gm_key_state(v) & bit) return 1; return 0; }   /* vk_anykey */
    return (gm_key_state(sub) & bit) != 0;
}
static void key_events(void)
{
    if (!gm_hk.key_state) return;
    if (!obj_haskey) obj_haskey = calloc(gm_nobjs ? gm_nobjs : 1, 1);
    int n = ninst;
    for (int k = 0; k < n; k++) {
        gm_inst *i = insts[k];
        if (i->dead) continue;
        if (!obj_haskey[i->obj]) {
            int has = 0;
            for (int o = i->obj, g = 0; o >= 0 && o < gm_nobjs && g < 32 && !has; g++, o = gm_objs[o].parent)
                for (int e = 0; e < gm_objs[o].nevents; e++) { int t = gm_objs[o].events[e].type; if (t == EV_KEYBOARD || t == EV_KEYPRESS || t == EV_KEYRELEASE) { has = 1; break; } }
            obj_haskey[i->obj] = has ? 1 : 2;
        }
        if (obj_haskey[i->obj] != 1) continue;
        static const int types[3] = {EV_KEYBOARD, EV_KEYPRESS, EV_KEYRELEASE};
        for (int ti = 0; ti < 3; ti++) {
            for (int o = i->obj, g = 0; o >= 0 && o < gm_nobjs && g < 32 && !i->dead; g++, o = gm_objs[o].parent) {
                const gm_objdef *d = &gm_objs[o];
                for (int e = 0; e < d->nevents && !i->dead; e++) {
                    if (d->events[e].type != types[ti]) continue;
                    int sub = d->events[e].sub, over = 0;
                    for (int po = i->obj; po != o && po >= 0 && !over; po = gm_objs[po].parent)      /* a descendant's event of the same key overrides */
                        for (int pe = 0; pe < gm_objs[po].nevents; pe++) if (gm_objs[po].events[pe].type == types[ti] && gm_objs[po].events[pe].sub == sub) { over = 1; break; }
                    if (over || !key_hit(types[ti], sub)) continue;
                    call_event(i, d->events[e].fn, o, types[ti], sub, NULL);
                }
            }
        }
    }
}

static void run_all(int type, int sub)
{
    int n = ninst;                                                  /* instances created during the loop wait for the next frame */
    for (int k = 0; k < n; k++) {
        gm_inst *i = insts[k];
        if (!i->dead) gm_run_event(i, type, sub);
    }
}

void gm_goto_room(int room)
{
    static const char *tr = (const char *)-1;
    if (tr == (const char *)-1) tr = getenv("GM_TRACE_ROOM");
    (void)tr;
    if (cur_ev.obj >= 0 && cur_ev.obj < gm_nobjs) gm_logf("room_goto %d from %s ev %d/%d frame %d", room, gm_objs[cur_ev.obj].name, cur_ev.type, cur_ev.sub, gm_frame);
    else gm_logf("room_goto %d (no event ctx) frame %d", room, gm_frame);
    gm_room_next = room;
}

static int game_started;
static int view_any;
/* persistent rooms: instances parked while another room is active */
static gm_inst ***stash_i; static int *stash_n; static float *stash_vw;   /* + the view positions of each parked room (xview,yview x 8) */
static gm_real_t gview(const char *n, int idx);
static void sview(const char *n, int idx, gm_real_t v);
static int room_is_persistent(void)
{
    int v = gm_vid("room_persistent");
    return v >= 0 && gm_truth(gm_get(gm_global_inst, v));
}
static void stash_room(int room)
{
    if (room < 0 || room >= gm_nrooms) return;
    if (!stash_i) { stash_i = calloc(gm_nrooms, sizeof *stash_i); stash_n = calloc(gm_nrooms, sizeof *stash_n); stash_vw = calloc((size_t)gm_nrooms * 16, sizeof *stash_vw); }
    for (int v = 0; v < 8; v++) { stash_vw[room * 16 + v * 2] = (float)gview("view_xview", v); stash_vw[room * 16 + v * 2 + 1] = (float)gview("view_yview", v); }
    int n = 0, w = 0;
    for (int k = 0; k < ninst; k++) if (!insts[k]->persistent && !insts[k]->dead) n++;
    gm_inst **a = malloc((n ? n : 1) * sizeof *a);
    int m = 0;
    for (int k = 0; k < ninst; k++) {
        if (!insts[k]->persistent && !insts[k]->dead) a[m++] = insts[k]; else insts[w++] = insts[k];
    }
    ninst = w; inst_epoch++;
    for (int k = 0; k < stash_n[room]; k++) { stash_i[room][k]->dead = 1; free_inst(stash_i[room][k]); }
    free(stash_i[room]);
    stash_i[room] = a; stash_n[room] = m;
}
static int unstash_room(int room)
{
    if (!stash_i || room < 0 || room >= gm_nrooms || !stash_i[room]) return 0;
    for (int k = 0; k < stash_n[room]; k++) {
        if (ninst == capinst) { capinst = capinst ? capinst * 2 : 256; insts = realloc(insts, capinst * sizeof *insts); }
        insts[ninst++] = stash_i[room][k]; inst_epoch++;
    }
    free(stash_i[room]); stash_i[room] = NULL; stash_n[room] = 0;
    for (int v = 0; v < 8; v++) { sview("view_xview", v, stash_vw[room * 16 + v * 2]); sview("view_yview", v, stash_vw[room * 16 + v * 2 + 1]); }   /* the room comes back as it was left, views included */
    return 1;
}
static void camera_follow(void);
static void room_change(void)
{
    int next = gm_room_next;
    gm_room_next = -1;
    if (gm_room_cur >= 0) {
        run_all(EV_OTHER, EVO_ROOM_END);
        if (gm_hk.room_end) gm_hk.room_end(gm_room_cur);
        if (room_is_persistent()) stash_room(gm_room_cur);      /* room_persistent: the room's instances are kept and come back untouched */
        else for (int k = 0; k < ninst; k++) if (!insts[k]->persistent) insts[k]->dead = 1;
        sweep_dead();
    }
    gm_room_cur = next;
    gm_val v = gm_real(next);
    gm_vtab_set(&gm_global_inst->vars, gm_vid("room"), v);
    if (next >= 0 && next < gm_nrooms) {
        const gm_roomdef *r = &gm_rooms[next];
        gm_vtab_set(&gm_global_inst->vars, gm_vid("room_width"), gm_real(r->w));
        gm_vtab_set(&gm_global_inst->vars, gm_vid("room_height"), gm_real(r->h));
        gm_room_speed = r->speed;
        {
            static const char *const VN[] = {"view_visible", "view_xview", "view_yview", "view_wview", "view_hview", "view_xport", "view_yport",
                                             "view_wport", "view_hport", "view_hborder", "view_vborder", "view_hspeed", "view_vspeed", "view_object"};
            int any = 0;
            for (int v = 0; v < 8; v++) {
                const int *f = &r->views[v].enabled;
                any |= f[0] != 0;
                for (int j = 0; j < 14; j++) gm_seta(gm_global_inst, gm_vid(VN[j]), gm_real(v), gm_real(j == 0 ? (f[0] != 0) : f[j]));
                gm_seta(gm_global_inst, gm_vid("view_angle"), gm_real(v), gm_real(0));
            }
            view_any = any;
            gm_vtab_set(&gm_global_inst->vars, gm_vid("view_enabled"), gm_real(any));
        }
        int restored = unstash_room(next);
        { int pv = gm_vid("room_persistent"); if (pv >= 0) gm_vtab_set(&gm_global_inst->vars, pv, gm_real(restored ? 1 : r->persistent)); }
        if (restored) goto room_ready;
        /* all room instances exist before any Create event runs (Create code looks up other instances, e.g. the entrance markers) */
        gm_inst **made = malloc((r->ninst ? r->ninst : 1) * sizeof *made);
        for (int k = 0; k < r->ninst; k++) {
            const gm_roominst *ri = &r->inst[k];
            made[k] = (ri->obj < 0 || ri->obj >= gm_nobjs) ? NULL : create_id(ri->x, ri->y, ri->obj, ri->id, 0);
        }
        for (int k = 0; k < r->ninst; k++) {
            gm_inst *i = made[k];
            if (!i || i->dead) continue;
            gm_run_event(i, EV_CREATE, 0); i->created = 1;
            if (r->inst[k].code && !i->dead) r->inst[k].code(i, i, 0, NULL);
        }
        free(made);
        if (r->code) r->code(gm_global_inst, gm_global_inst, 0, NULL);
    }
    room_ready:;
    if (gm_hk.room_start) gm_hk.room_start(next);
    if (!game_started) { game_started = 1; run_all(EV_OTHER, EVO_GAME_START); }
    run_all(EV_OTHER, EVO_ROOM_START);
    camera_follow();
}

static const char *const VIEWN[] = {"view_visible", "view_xview", "view_yview", "view_wview", "view_hview", "view_xport", "view_yport",
                                    "view_wport", "view_hport", "view_hborder", "view_vborder", "view_hspeed", "view_vspeed", "view_object"};
/* a view property: the game's own variable when the game uses it, otherwise the room's definition (variables no script mentions are not in the table) */
static gm_real_t gview(const char *n, int idx)
{
    int vid = gm_vid(n);
    if (vid >= 0) return gm_to_real(gm_geta(gm_global_inst, vid, gm_real(idx)));
    if (gm_room_cur < 0 || gm_room_cur >= gm_nrooms) return 0;
    for (int j = 0; j < 14; j++) if (!strcmp(VIEWN[j], n)) return (&gm_rooms[gm_room_cur].views[idx].enabled)[j];
    return 0;
}
static void sview(const char *n, int idx, gm_real_t v) { int vid = gm_vid(n); if (vid >= 0) gm_seta(gm_global_inst, vid, gm_real(idx), gm_real(v)); }
/* views that follow an object: keep it inside the border, at most hspeed/vspeed px per step (-1 = instant), clamped to the room */
static void camera_follow(void)
{
    if (gm_room_cur < 0 || gm_room_cur >= gm_nrooms) return;
    if (!view_any) return;
    for (int v = 0; v < 8; v++) {
        if (!gview("view_visible", v)) continue;
        gm_real_t vx = gview("view_xview", v), vy = gview("view_yview", v), vw = gview("view_wview", v), vh = gview("view_hview", v);
        int obj = (int)gview("view_object", v);
        gm_inst *t = obj >= 100000 ? gm_instance_by_id(obj) : (obj >= 0 ? gm_obj1(obj) : NULL);
        if (t) {
            gm_real_t l, tp, r, b, hb = gview("view_hborder", v), vb = gview("view_vborder", v), hs = gview("view_hspeed", v), vs = gview("view_vspeed", v);
            if (!gm_inst_bbox(t, &l, &tp, &r, &b)) { l = r = t->x; tp = b = t->y; }
            gm_real_t nx = vx, ny = vy;
            if (r + hb > vx + vw) nx = r + hb - vw;
            if (l - hb < nx) nx = l - hb;
            if (b + vb > vy + vh) ny = b + vb - vh;
            if (tp - vb < ny) ny = tp - vb;
            if (hs >= 0 && fabs(nx - vx) > hs) nx = vx + (nx > vx ? hs : -hs);
            if (vs >= 0 && fabs(ny - vy) > vs) ny = vy + (ny > vy ? vs : -vs);
            vx = nx; vy = ny;
        }
        if (t) {                                       /* only a view that follows an object is kept inside the room: cutscenes may pan or zoom outside it */
            const gm_roomdef *rd = &gm_rooms[gm_room_cur];
            if (vx > rd->w - vw) vx = rd->w - vw;
            if (vy > rd->h - vh) vy = rd->h - vh;
            if (vx < 0) vx = 0;
            if (vy < 0) vy = 0;
        }
        sview("view_xview", v, vx); sview("view_yview", v, vy);
    }
}

int gm_camera(int *x, int *y)          /* top-left of view 0 in room coordinates; 0 when the room has no visible view */
{
    *x = *y = 0;
    if (!view_any || !gview("view_visible", 0)) return 0;
    *x = (int)gview("view_xview", 0); *y = (int)gview("view_yview", 0);
    return 1;
}

int gm_camera_view(int *w, int *h)      /* size of view 0 (zoom effects shrink it below the port size); 0 when there is no visible view */
{
    if (!view_any || !gview("view_visible", 0)) return 0;
    *w = (int)gview("view_wview", 0); *h = (int)gview("view_hview", 0);
    return *w > 0 && *h > 0;
}

/* ---- paths: polyline sampled from the path points (smooth = Catmull-Rom), position 0..1 along its length ---- */
#define PSUB 8
typedef struct { int n; float *xy; float *cum; } pcache_t;
static pcache_t pcache[64];
static pcache_t *pc_get(int p)
{
    if (p < 0 || p >= gm_npaths || p >= 64) return NULL;
    pcache_t *c = &pcache[p];
    if (c->xy) return c;
    const gm_pathdef *d = &gm_paths[p];
    int np = d->n, tot = d->smooth ? (np - 1) * PSUB + 1 : np;
    if (np < 1) return NULL;
    c->xy = malloc(sizeof(float) * 2 * (tot + 1)); c->cum = malloc(sizeof(float) * (tot + 1)); c->n = 0;
    for (int k = 0; k < np - 1 || k == 0; k++) {
        int steps = d->smooth ? PSUB : 1;
        for (int s = 0; s < steps && (np > 1 || s == 0); s++) {
            float t = (float)s / steps, x, y;
            if (d->smooth) {
                const float *P0 = &d->pts[3 * (k > 0 ? k - 1 : 0)], *P1 = &d->pts[3 * k], *P2 = &d->pts[3 * (k + 1 < np ? k + 1 : k)], *P3 = &d->pts[3 * (k + 2 < np ? k + 2 : np - 1)];
                float t2 = t * t, t3 = t2 * t;
                x = 0.5f * ((2 * P1[0]) + (-P0[0] + P2[0]) * t + (2 * P0[0] - 5 * P1[0] + 4 * P2[0] - P3[0]) * t2 + (-P0[0] + 3 * P1[0] - 3 * P2[0] + P3[0]) * t3);
                y = 0.5f * ((2 * P1[1]) + (-P0[1] + P2[1]) * t + (2 * P0[1] - 5 * P1[1] + 4 * P2[1] - P3[1]) * t2 + (-P0[1] + 3 * P1[1] - 3 * P2[1] + P3[1]) * t3);
            } else { x = d->pts[3 * k]; y = d->pts[3 * k + 1]; }
            c->xy[2 * c->n] = x; c->xy[2 * c->n + 1] = y; c->n++;
        }
    }
    c->xy[2 * c->n] = d->pts[3 * (np - 1)]; c->xy[2 * c->n + 1] = d->pts[3 * (np - 1) + 1]; c->n++;
    if (d->closed && c->n > 1) { c->xy[2 * c->n] = c->xy[0]; c->xy[2 * c->n + 1] = c->xy[1]; c->n++; }
    c->cum[0] = 0;
    for (int k = 1; k < c->n; k++) c->cum[k] = c->cum[k - 1] + hypotf(c->xy[2 * k] - c->xy[2 * k - 2], c->xy[2 * k + 1] - c->xy[2 * k - 1]);
    return c;
}
static void path_pt(const pcache_t *c, gm_real_t pos, gm_real_t *x, gm_real_t *y)
{
    float L = c->cum[c->n - 1], d = (float)pos * L;
    int k = 1;
    while (k < c->n - 1 && c->cum[k] < d) k++;
    if (c->n < 2 || L <= 0) { *x = c->xy[0]; *y = c->xy[1]; return; }
    float seg = c->cum[k] - c->cum[k - 1], t = seg > 0 ? (d - c->cum[k - 1]) / seg : 0;
    *x = c->xy[2 * k - 2] + (c->xy[2 * k] - c->xy[2 * k - 2]) * t;
    *y = c->xy[2 * k - 1] + (c->xy[2 * k + 1] - c->xy[2 * k - 1]) * t;
}
void gm_path_start(gm_inst *i, int path, gm_real_t speed, int endaction, int absolute)
{
    pcache_t *c = pc_get(path);
    if (!c) return;
    i->path_on = 1; i->path_idx = path; i->path_spd = speed; i->path_end = endaction; i->path_abs = absolute;
    i->path_pos = speed < 0 ? 1 : 0;
    gm_real_t px, py; path_pt(c, i->path_pos, &px, &py);
    if (absolute) { i->path_ox = 0; i->path_oy = 0; }
    else { i->path_ox = i->x - px; i->path_oy = i->y - py; }
    i->x = px + i->path_ox; i->y = py + i->path_oy;
}
void gm_path_stop(gm_inst *i) { i->path_on = 0; }
static void path_step(gm_inst *i)
{
    pcache_t *c = pc_get(i->path_idx);
    if (!c) { i->path_on = 0; return; }
    float L = c->cum[c->n - 1];
    if (L <= 0) { i->path_on = 0; return; }
    i->path_pos += i->path_spd / L;
    int out = (i->path_spd > 0 && i->path_pos >= 1) || (i->path_spd < 0 && i->path_pos <= 0);   /* a path at speed 0 just waits */
    if (out) {
        gm_real_t px, py;
        switch (i->path_end) {
        case 1: i->path_pos = i->path_spd < 0 ? 1 : 0; path_pt(c, i->path_pos, &px, &py); if (!i->path_abs) { i->path_ox = i->x - px; i->path_oy = i->y - py; } break;
        case 2: { gm_real_t ex, ey; path_pt(c, i->path_pos >= 1 ? 1 : 0, &ex, &ey); i->path_ox = i->x - ex; i->path_oy = i->y - ey; (void)ex; i->path_pos = i->path_spd < 0 ? 1 : 0; path_pt(c, i->path_pos, &px, &py); i->path_ox = i->x - px; i->path_oy = i->y - py; break; }
        case 3: i->path_spd = -i->path_spd; i->path_pos = i->path_pos >= 1 ? 1 : 0; break;
        default: i->path_pos = i->path_pos >= 1 ? 1 : 0; i->path_on = 0; break;
        }
    }
    gm_real_t px, py; path_pt(c, i->path_pos, &px, &py);
    gm_real_t nx = px + i->path_ox, ny = py + i->path_oy, ddx = nx - i->x, ddy = ny - i->y;
    if (fabs(ddx) + fabs(ddy) > 1e-4) {                         /* following a path updates `direction` (NPC sprites pick their facing from it) */
        gm_real_t d = atan2(-ddy, ddx) * 180.0 / 3.14159265358979323846;
        i->direction = d < 0 ? d + 360 : d;
    }
    i->x = nx; i->y = ny;
}

/* Outside Room / Intersect Boundary events (after movement) */
static void outside_events(void)
{
    if (gm_room_cur < 0 || gm_room_cur >= gm_nrooms) return;
    const gm_roomdef *rd = &gm_rooms[gm_room_cur];
    int n = ninst;
    for (int k = 0; k < n; k++) {
        gm_inst *i = insts[k];
        if (i->dead) continue;
        int hout = gm_find_event_fn(i->obj, EV_OTHER, EVO_OUTSIDE, NULL) != NULL, hbnd = gm_find_event_fn(i->obj, EV_OTHER, EVO_BOUNDARY, NULL) != NULL;
        if (!hout && !hbnd) continue;
        gm_real_t l, t, r, b;
        if (!gm_inst_bbox(i, &l, &t, &r, &b)) { l = r = i->x; t = b = i->y; }
        if (hbnd && (l < 0 || t < 0 || r >= rd->w || b >= rd->h)) gm_run_event(i, EV_OTHER, EVO_BOUNDARY);
        if (hout && !i->dead && (r < 0 || b < 0 || l >= rd->w || t >= rd->h)) gm_run_event(i, EV_OTHER, EVO_OUTSIDE);
    }
}

extern void gm_audio_tick(void) __attribute__((weak));
void gm_step(void)
{
    for (int k = 0; k < ninst; k++) { insts[k]->xprev = insts[k]->x; insts[k]->yprev = insts[k]->y; }   /* xprevious = position at step start */
    run_all(EV_STEP, 1);                                            /* begin step */
    int n = ninst;
    for (int k = 0; k < n; k++) {                                   /* alarms */
        gm_inst *i = insts[k];
        for (int a = 0; a < GM_MAX_ALARMS && !i->dead; a++)
            if (i->alarm[a] > 0 && --i->alarm[a] == 0) { i->alarm[a] = -1; gm_run_event(i, EV_ALARM, a); }
    }
    key_events();
    run_all(EV_STEP, 0);
    n = ninst;
    for (int k = 0; k < n; k++) {                                   /* movement */
        gm_inst *i = insts[k];
        if (i->dead) continue;
        if (i->friction != 0) {
            gm_real_t sp = speed_of(i);
            if (sp > 0) { gm_real_t ns = sp > i->friction ? sp - i->friction : 0; set_polar(i, ns, dir_of(i)); }
        }
        if (i->gravity != 0) {
            gm_real_t r = i->gravity_dir * 3.14159265358979323846 / 180.0;
            i->hspeed += i->gravity * cos(r);
            i->vspeed += -i->gravity * sin(r);
        }
        i->x += i->hspeed; i->y += i->vspeed;
        if (i->path_on) path_step(i);
        if (i->image_speed != 0 && i->sprite >= 0 && i->sprite < gm_nsprites && gm_sprites[i->sprite].nframes > 0) {
            int nf = gm_sprites[i->sprite].nframes;
            i->image_index += i->image_speed;
            if (i->image_index >= nf || i->image_index < 0) {                /* the animation wrapped: Animation End event */
                i->image_index -= nf * floor(i->image_index / nf);
                if (gm_find_event_fn(i->obj, EV_OTHER, EVO_ANIM_END, NULL)) i->anim_end = 1;
            }
        } else i->image_index += i->image_speed;
    }
    for (int k = 0; k < n; k++) {                                   /* animation end events (after all movement) */
        gm_inst *i = insts[k];
        if (i->anim_end) { i->anim_end = 0; if (!i->dead) gm_run_event(i, EV_OTHER, EVO_ANIM_END); }
    }
    outside_events();
    gm_collision_events();
    run_all(EV_STEP, 2);                                            /* end step */
    camera_follow();
    sweep_dead();
    if (gm_room_next >= 0) room_change();
    if (gm_audio_tick) gm_audio_tick();
    gm_frame++;
}

/* optional per-object draw-event profiler (set by the platform main): cycles + calls per object, dumped by gm_prof_dump */
unsigned (*gm_prof_cyc)(void);
static unsigned long long fprof_c[8]; static unsigned fprof_n[8];
static const char *const FPN[8] = {"fn:draw_circle", "fn:collision_circle", "fn:gm_obj1", "fn:ge_gfx_flat", "fn:slot4", "fn:slot5", "fn:slot6", "fn:slot7"};
void gm_prof_fn(int slot, unsigned cyc) { fprof_c[slot & 7] += cyc; fprof_n[slot & 7]++; }
static unsigned long long prof_c[4096]; static unsigned prof_n[4096];
void gm_prof_dump(void (*out)(const char *name, unsigned long long cyc, unsigned calls))
{
    for (int pass = 0; pass < 6; pass++) {
        int best = -1;
        for (int o = 0; o < gm_nobjs && o < 4096; o++) if (prof_c[o] && (best < 0 || prof_c[o] > prof_c[best])) best = o;
        if (best < 0) break;
        out(gm_objs[best].name, prof_c[best], prof_n[best]); prof_c[best] = 0; prof_n[best] = 0;
    }
    for (int k = 0; k < 8; k++) if (fprof_n[k]) { out(FPN[k], fprof_c[k], fprof_n[k]); fprof_c[k] = 0; fprof_n[k] = 0; }
    memset(prof_c, 0, sizeof prof_c); memset(prof_n, 0, sizeof prof_n);
}
void gm_draw(void)
{
    int n = ninst;
    gm_inst **order = malloc((n ? n : 1) * sizeof *order);
    int m = 0;
    for (int k = 0; k < n; k++) if (!insts[k]->dead && insts[k]->visible) order[m++] = insts[k];
    qsort(order, m, sizeof *order, cmp_depth);
    if (gm_hk.draw_begin) gm_hk.draw_begin();
    for (int k = 0; k < m; k++) {
        gm_inst *i = order[k];
        if (i->dead) continue;
        if (gm_hk.draw_layers) gm_hk.draw_layers(-i->depth);              /* tiles / backgrounds behind this instance */
        unsigned pc0 = gm_prof_cyc ? gm_prof_cyc() : 0;
        if (gm_find_event_fn(i->obj, EV_DRAW, 0, NULL)) gm_run_event(i, EV_DRAW, 0);
        else if (gm_hk.draw_self) gm_hk.draw_self(i);
        if (gm_prof_cyc && i->obj >= 0 && i->obj < 4096) { prof_c[i->obj] += (unsigned)(gm_prof_cyc() - pc0); prof_n[i->obj]++; }
    }
    if (gm_hk.draw_layers) gm_hk.draw_layers(0x7FFFFFFF);
    if (gm_hk.draw_end) gm_hk.draw_end();
    free(order);
    gm_frame_end();
}

/* ---------------- init ---------------- */
void gm_init(void)
{
    int cap = 1;
    while (cap < gm_nvars * 2 + 2) cap <<= 1;
    name_hash_cap = cap;
    name_hash = calloc(cap, sizeof(int));
    for (int v = 0; v < gm_nvars; v++) {
        unsigned h = hstr(gm_var_names[v]) & (cap - 1);
        while (name_hash[h]) h = (h + 1) & (cap - 1);
        name_hash[h] = v + 1;
    }
    bikind = calloc(gm_nvars + 1, 1);
    bioff = calloc(gm_nvars + 1, sizeof(short));
    for (size_t k = 0; k < sizeof BI / sizeof BI[0]; k++) {
        int v = gm_vid(BI[k].name);
        if (v >= 0) { bikind[v] = BI[k].kind; bioff[v] = BI[k].off; }
    }
    vid_alarm = gm_vid("alarm");
    isglob = calloc(gm_nvars + 1, 1);
    static const char *const GB[] = {"room", "room_speed", "room_width", "room_height", "score", "lives", "health", "fps", "view_current",
                                     "mouse_x", "mouse_y", "current_time", "keyboard_key", "keyboard_lastkey", "keyboard_string", "argument_relative",
                                     "room_persistent", "background_color", "background_visible", "background_index", "background_x", "background_y", "background_htiled", "background_vtiled", "background_foreground", "background_alpha", "background_hspeed", "background_vspeed", "background_blend", "background_stretch", "view_enabled", "view_xview", "view_yview", "view_wview", "view_hview", "view_xport", "view_yport", "view_wport", "view_hport", "view_visible", "view_object", "view_hborder", "view_vborder", "view_hspeed", "view_vspeed", "view_angle", "show_score", "show_lives", "show_health", "caption_score", "caption_lives", "caption_health"};
    for (size_t k = 0; k < sizeof GB / sizeof GB[0]; k++) { int v = gm_vid(GB[k]); if (v >= 0) isglob[v] = 1; }
    memset(&global_store, 0, sizeof global_store);
    global_store.id = -5;
    static const struct { const char *n; double v; } G[] = {
        {"room_speed", 30}, {"score", 0}, {"lives", 3}, {"health", 100}, {"room", -1}, {"room_width", 640}, {"room_height", 480},
        {"fps", 30}, {"argument_relative", 0}, {"view_current", 0},
    };
    for (size_t k = 0; k < sizeof G / sizeof G[0]; k++) {
        int v = gm_vid(G[k].n);
        if (v >= 0) gm_vtab_set(&global_store.vars, v, gm_real(G[k].v));
    }
    for (int r = 0; r < gm_nrooms; r++)
        for (int k = 0; k < gm_rooms[r].ninst; k++)
            if (gm_rooms[r].inst[k].id >= next_id) next_id = gm_rooms[r].inst[k].id + 1;
    gm_lib_init();
}
