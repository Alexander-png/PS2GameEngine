/* gml_core.c - GML values: strings (constant / scratch / heap), operators, conversions, arrays. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "gml/gml_rt.h"

/* ---------------- strings ---------------- */
#define ARENA_SIZE (512 * 1024)
static char arena[ARENA_SIZE] __attribute__((aligned(16)));
static size_t arena_pos;
static gm_str **deferred;
static int ndeferred, capdeferred;

gm_str *gm_str_new(const char *s, int len)
{
    size_t need = (sizeof(gm_str) + len + 1 + 15) & ~(size_t)15;
    gm_str *r;
    char *d;
    if (arena_pos + need <= ARENA_SIZE) {
        r = (gm_str *)(arena + arena_pos);
        arena_pos += need;
        r->rc = 0;
    } else {                                              /* arena exhausted: a heap string freed at the end of the frame */
        r = malloc(need);
        r->rc = 1;
        if (ndeferred == capdeferred) { capdeferred = capdeferred ? capdeferred * 2 : 256; deferred = realloc(deferred, capdeferred * sizeof *deferred); }
        deferred[ndeferred++] = r;
    }
    d = (char *)(r + 1);
    if (s) memcpy(d, s, len);
    d[len] = 0;
    r->len = len;
    r->s = d;
    return r;
}
gm_val gm_str_make(const char *s, int len) { return gm_str_val(gm_str_new(s, len)); }
gm_val gm_str_cstr(const char *s) { return gm_str_make(s, (int)strlen(s)); }

gm_val gm_promote(gm_val v)
{
    if (v.t == GM_STR) {
        gm_str *s = v.u.s;
        if (s->rc > 0) { if (s >= (gm_str *)arena && s < (gm_str *)(arena + ARENA_SIZE)) { } s->rc++; }
        else if (s->rc == 0) {
            gm_str *h = malloc(sizeof(gm_str) + s->len + 1);
            char *d = (char *)(h + 1);
            memcpy(d, s->s, s->len + 1);
            h->rc = 1; h->len = s->len; h->s = d;
            v.u.s = h;
        }
    } else if (v.t == GM_ARR) {
        v.u.a = gm_arr_copy(v.u.a);
    }
    return v;
}

void gm_release(gm_val v)
{
    if (v.t == GM_STR && v.u.s->rc > 0) {
        if (--v.u.s->rc == 0) {
            if (ndeferred == capdeferred) { capdeferred = capdeferred ? capdeferred * 2 : 256; deferred = realloc(deferred, capdeferred * sizeof *deferred); }
            deferred[ndeferred++] = v.u.s;
        }
    } else if (v.t == GM_ARR) {
        gm_arr_free(v.u.a);
    }
}

void gm_frame_end(void)
{
    for (int i = 0; i < ndeferred; i++) if (deferred[i]->rc <= 0) free(deferred[i]);
    ndeferred = 0;
    arena_pos = 0;
}

/* ---------------- arrays (GML 2-D index = row * 32000 + column) ---------------- */
static void arr_grow(gm_arr *a, int n)
{
    if (n <= a->n) return;
    a->v = realloc(a->v, n * sizeof(gm_val));
    for (int i = a->n; i < n; i++) a->v[i] = GM_UNDEF_V;
    a->n = n;
}
gm_arr *gm_arr_new(void) { return calloc(1, sizeof(gm_arr)); }
void gm_arr_free(gm_arr *a)
{
    if (!a) return;
    for (int i = 0; i < a->n; i++) gm_release(a->v[i]);
    free(a->v);
    for (int r = 0; r < a->nrows; r++) gm_arr_free(a->rows[r]);
    free(a->rows);
    free(a);
}
gm_arr *gm_arr_copy(const gm_arr *a)
{
    gm_arr *c = gm_arr_new();
    if (!a) return c;
    arr_grow(c, a->n);
    for (int i = 0; i < a->n; i++) c->v[i] = gm_promote(a->v[i]);
    c->nrows = a->nrows;
    c->rows = a->nrows ? calloc(a->nrows, sizeof(gm_arr *)) : NULL;
    for (int r = 0; r < a->nrows; r++) c->rows[r] = a->rows[r] ? gm_arr_copy(a->rows[r]) : NULL;
    return c;
}
gm_val gm_arr_get(const gm_arr *a, int idx)
{
    if (!a || idx < 0) return GM_UNDEF_V;
    if (idx >= 32000) {
        int r = idx / 32000 - 1;
        if (r >= a->nrows || !a->rows[r]) return GM_UNDEF_V;
        return gm_arr_get(a->rows[r], idx % 32000);
    }
    return idx < a->n ? a->v[idx] : GM_UNDEF_V;
}
void gm_arr_set(gm_arr *a, int idx, gm_val v)
{
    if (idx < 0) return;
    if (idx >= 32000) {
        int r = idx / 32000 - 1;
        if (r >= a->nrows) { a->rows = realloc(a->rows, (r + 1) * sizeof(gm_arr *)); for (int k = a->nrows; k <= r; k++) a->rows[k] = NULL; a->nrows = r + 1; }
        if (!a->rows[r]) a->rows[r] = gm_arr_new();
        gm_arr_set(a->rows[r], idx % 32000, v);
        return;
    }
    arr_grow(a, idx + 1);
    gm_release(a->v[idx]);
    a->v[idx] = gm_promote(v);
}

/* ---------------- conversions ---------------- */
gm_val gm_format_real(gm_real_t r)
{
    char b[48];
    if (r == (gm_real_t)(long long)r && fabs(r) < 1e15) snprintf(b, sizeof b, "%lld", (long long)r);
    else {
        snprintf(b, sizeof b, "%.2f", (double)r);
        char *e = b + strlen(b);
        while (e > b && e[-1] == '0') *--e = 0;
        if (e > b && e[-1] == '.') *--e = 0;
        if (!strcmp(b, "-0")) strcpy(b, "0");
    }
    return gm_str_cstr(b);
}
gm_real_t gm_to_real(gm_val v)
{
    if (v.t == GM_REAL) return v.u.r;
    if (v.t == GM_STR) return strtod(v.u.s->s, NULL);
    return 0;
}
int gm_to_int(gm_val v)
{
    gm_real_t r = gm_to_real(v);
    return (int)(r >= 0 ? floor(r + 0.5 * 0) : ceil(r));                 /* truncation like a C cast for non-integers */
}
const char *gm_cstr(gm_val v, char *tmp, int tmpsz)
{
    if (v.t == GM_STR) return v.u.s->s;
    if (v.t == GM_REAL) {
        gm_val s = gm_format_real(v.u.r);
        snprintf(tmp, tmpsz, "%s", s.u.s->s);
        return tmp;
    }
    return "";
}
int gm_truth_slow(gm_val v) { return v.t == GM_REAL ? v.u.r > 0.5 : 0; }

gm_val gm_conv_i(gm_val a) { return a.t == GM_REAL ? gm_real(trunc(a.u.r)) : gm_real(gm_to_real(a)); }
gm_val gm_conv_b(gm_val a) { return gm_real(gm_truth(a) ? 1 : 0); }
gm_val gm_conv_s(gm_val a) { return a.t == GM_STR ? a : (a.t == GM_REAL ? gm_format_real(a.u.r) : gm_str_cstr("")); }

/* ---------------- operators ---------------- */
gm_val gm_add_slow(gm_val a, gm_val b)
{
    if (a.t == GM_STR || b.t == GM_STR) {
        char t1[48], t2[48];
        const char *x = gm_cstr(a, t1, sizeof t1), *y = gm_cstr(b, t2, sizeof t2);
        int lx = (int)strlen(x), ly = (int)strlen(y);
        gm_str *r = gm_str_new(NULL, lx + ly);
        memcpy((char *)r->s, x, lx);
        memcpy((char *)r->s + lx, y, ly);
        return gm_str_val(r);
    }
    return gm_real(gm_to_real(a) + gm_to_real(b));
}
gm_val gm_arith_slow(int op, gm_val a, gm_val b)
{
    gm_real_t x = gm_to_real(a), y = gm_to_real(b);
    return gm_real(op == 1 ? x - y : x * y);
}
gm_val gm_div(gm_val a, gm_val b)
{
    gm_real_t y = gm_to_real(b);
    if (y == 0) return gm_real(0);
    return gm_real(gm_to_real(a) / y);
}
gm_val gm_idiv(gm_val a, gm_val b)
{
    gm_real_t y = trunc(gm_to_real(b));
    if (y == 0) return gm_real(0);
    return gm_real(trunc(trunc(gm_to_real(a)) / y));
}
gm_val gm_mod(gm_val a, gm_val b)
{
    gm_real_t y = gm_to_real(b);
    if (y == 0) return gm_real(0);
    return gm_real(fmod(gm_to_real(a), y));
}
static int64_t ival(gm_val v) { return (int64_t)gm_to_real(v); }
gm_val gm_and(gm_val a, gm_val b) { return gm_real((gm_real_t)(ival(a) & ival(b))); }
gm_val gm_or(gm_val a, gm_val b) { return gm_real((gm_real_t)(ival(a) | ival(b))); }
gm_val gm_xor(gm_val a, gm_val b) { return gm_real((gm_real_t)(ival(a) ^ ival(b))); }
gm_val gm_shl(gm_val a, gm_val b) { return gm_real((gm_real_t)(ival(a) << (ival(b) & 63))); }
gm_val gm_shr(gm_val a, gm_val b) { return gm_real((gm_real_t)(ival(a) >> (ival(b) & 63))); }
gm_val gm_neg(gm_val a) { return gm_real(-gm_to_real(a)); }
gm_val gm_lnot(gm_val a) { return gm_real(gm_truth(a) ? 0 : 1); }
gm_val gm_bnot(gm_val a) { return gm_real((gm_real_t)(~ival(a))); }
gm_val gm_cmp_slow(int op, gm_val a, gm_val b)
{
    int c;
    if (a.t == GM_STR && b.t == GM_STR) c = strcmp(a.u.s->s, b.u.s->s);
    else if (a.t == GM_STR || b.t == GM_STR) c = (op == 3) ? 1 : (op == 4 ? 1 : 0);   /* string vs number: never equal */
    else { gm_real_t x = gm_to_real(a), y = gm_to_real(b); c = x < y ? -1 : (x > y ? 1 : 0); }
    int r;
    switch (op) {
    case 1: r = c < 0; break;
    case 2: r = c <= 0; break;
    case 3: r = c == 0; break;
    case 4: r = c != 0; break;
    case 5: r = c >= 0; break;
    default: r = c > 0; break;
    }
    if ((a.t == GM_STR) != (b.t == GM_STR)) r = (op == 4);
    return gm_real(r);
}

/* ---------------- diagnostics ---------------- */
void gm_logf(const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (gm_hk.log) gm_hk.log(b); else fprintf(stderr, "%s\n", b);
}

gm_val gm_keep(gm_val v) { return v.t == GM_ARR ? gm_promote(v) : v; }

gm_val gm_unimpl(const char *name)
{
    static const char *seen[2048];
    static int nseen;
    for (int i = 0; i < nseen; i++) if (seen[i] == name) return GM_UNDEF_V;
    if (nseen < 2048) seen[nseen++] = name;
    gm_logf("unimplemented function: %s", name);
    return GM_UNDEF_V;
}
void gm_unsupported(const char *entry) { gm_logf("unsupported code entry executed: %s", entry); }
