/* gml_file.c - GML file_text_*, file_*, ini_*. Files are whole-file memory buffers; storage goes through gm_hk.fs_read/fs_write
 * (platform), with a stdio default (names relative to gm_save_dir) when no hook is set. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gml/gml_rt.h"

char gm_save_dir[256] = "";
static char ini_cache_name[96];       /* the parsed ini stays in memory between ini_open/ini_close (re-parsing every step is far too slow on the EE) */
static int ini_cache_ok;
static void ini_invalidate(const char *name) { if (!strcmp(name, ini_cache_name)) ini_cache_ok = 0; }

static void *fs_read(const char *name, int *len)
{
    if (gm_hk.fs_read) return gm_hk.fs_read(name, len);
    char p[320];
    snprintf(p, sizeof p, "%s%s", gm_save_dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) n = 0;
    fclose(f);
    *len = (int)n;
    return b;
}
static int fs_write(const char *name, const void *data, int len)
{
    ini_invalidate(name);
    if (gm_hk.fs_write) return gm_hk.fs_write(name, data, len);
    char p[320];
    snprintf(p, sizeof p, "%s%s", gm_save_dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    fwrite(data, 1, len, f);
    fclose(f);
    return 0;
}
static int fs_exists(const char *name)
{
    int n;
    void *b = fs_read(name, &n);
    free(b);
    return b != NULL;
}
static const char *argstr(gm_val v, char *tmp, int n) { return gm_cstr(v, tmp, n); }

/* ---------------- file_text ---------------- */
#define MAXF 32
typedef struct { char *name; char *buf; int len, cap, pos; int wr; } tfile;
static tfile files[MAXF + 1];

static int new_file(const char *name, int wr)
{
    for (int k = 1; k <= MAXF; k++) if (!files[k].name) {
        tfile *f = &files[k];
        memset(f, 0, sizeof *f);
        f->name = strdup(name);
        f->wr = wr;
        if (!wr) {
            f->buf = fs_read(name, &f->len);
            if (!f->buf) { free(f->name); f->name = NULL; return -1; }
        }
        return k;
    }
    return -1;
}
static tfile *tf(gm_val v) { int h = gm_to_int(v); return (h >= 1 && h <= MAXF && files[h].name) ? &files[h] : NULL; }
static void wput(tfile *f, const char *s, int n)
{
    if (f->len + n > f->cap) { f->cap = (f->len + n) * 2 + 256; f->buf = realloc(f->buf, f->cap); }
    memcpy(f->buf + f->len, s, n);
    f->len += n;
}

GMB(file_text_open_read) { (void)self; (void)other; char t[96]; return gm_real(new_file(argstr(GM_ARG(0), t, sizeof t), 0)); }
GMB(file_text_open_write) { (void)self; (void)other; char t[96]; return gm_real(new_file(argstr(GM_ARG(0), t, sizeof t), 1)); }
GMB(file_text_close)
{
    (void)self; (void)other;
    tfile *f = tf(GM_ARG(0));
    if (!f) return GM_UNDEF_V;
    if (f->wr) fs_write(f->name, f->buf ? f->buf : "", f->len);
    free(f->buf); free(f->name);
    memset(f, 0, sizeof *f);
    return GM_UNDEF_V;
}
GMB(file_text_eof) { (void)self; (void)other; tfile *f = tf(GM_ARG(0)); return gm_real(!f || f->pos >= f->len); }
GMB(file_text_read_string)
{
    (void)self; (void)other;
    tfile *f = tf(GM_ARG(0));
    if (!f) return gm_str_make("", 0);
    int s = f->pos, e = s;
    while (e < f->len && f->buf[e] != '\n' && f->buf[e] != '\r') e++;
    f->pos = e;
    return gm_str_make(f->buf + s, e - s);
}
GMB(file_text_readln)
{
    (void)self; (void)other;
    tfile *f = tf(GM_ARG(0));
    if (!f) return GM_UNDEF_V;
    while (f->pos < f->len && f->buf[f->pos] != '\n') f->pos++;
    if (f->pos < f->len) f->pos++;
    return GM_UNDEF_V;
}
GMB(file_text_read_real)
{
    (void)self; (void)other;
    tfile *f = tf(GM_ARG(0));
    if (!f) return gm_real(0);
    while (f->pos < f->len && (f->buf[f->pos] == ' ' || f->buf[f->pos] == '\n' || f->buf[f->pos] == '\r' || f->buf[f->pos] == '\t')) f->pos++;
    char tmp[48]; int n = 0;
    while (f->pos < f->len && n < 47 && (f->buf[f->pos] == '-' || f->buf[f->pos] == '.' || f->buf[f->pos] == '+' || f->buf[f->pos] == 'e' ||
                                           (f->buf[f->pos] >= '0' && f->buf[f->pos] <= '9'))) tmp[n++] = f->buf[f->pos++];
    tmp[n] = 0;
    { static int nlog; if (nlog < 60 || f->pos > f->len - 24) { nlog++; gm_logf("read_real %s @%d '%s' = %g", f->name, f->pos, tmp, (double)atof(tmp)); } }
    return gm_real((gm_real_t)atof(tmp));
}
GMB(file_text_write_string)
{
    (void)self; (void)other;
    tfile *f = tf(GM_ARG(0));
    char t[64];
    if (f && f->wr) { const char *s = gm_cstr(GM_ARG(1), t, sizeof t); wput(f, s, (int)strlen(s)); }
    return GM_UNDEF_V;
}
GMB(file_text_write_real)
{
    (void)self; (void)other;
    tfile *f = tf(GM_ARG(0));
    char t[64];
    if (f && f->wr) { const char *s = gm_cstr(gm_conv_s(GM_ARG(1)), t, sizeof t); wput(f, s, (int)strlen(s)); }
    return GM_UNDEF_V;
}
GMB(file_text_writeln) { (void)self; (void)other; tfile *f = tf(GM_ARG(0)); if (f && f->wr) wput(f, "\n", 1); return GM_UNDEF_V; }

GMB(file_exists) { (void)self; (void)other; char t[96]; return gm_real(fs_exists(argstr(GM_ARG(0), t, sizeof t))); }
GMB(file_delete)
{
    (void)self; (void)other;
    char t[96], p[320];
    snprintf(p, sizeof p, "%s%s", gm_save_dir, argstr(GM_ARG(0), t, sizeof t));
    ini_invalidate(t);
    if (gm_hk.fs_write) return gm_real(gm_hk.fs_write(t, NULL, -1) == 0);        /* len -1 = delete */
    return gm_real(remove(p) == 0);
}
GMB(file_rename)
{
    (void)self; (void)other;
    char t1[96], t2[96];
    const char *a = argstr(GM_ARG(0), t1, sizeof t1), *b = argstr(GM_ARG(1), t2, sizeof t2);
    int n;
    ini_invalidate(a);
    void *d = fs_read(a, &n);
    if (!d) return gm_real(0);
    int ok = fs_write(b, d, n) == 0;
    free(d);
    if (ok) {
        if (gm_hk.fs_write) gm_hk.fs_write(a, NULL, -1);
        else { char p[320]; snprintf(p, sizeof p, "%s%s", gm_save_dir, a); remove(p); }
    }
    return gm_real(ok);
}
GMB(steam_file_delete) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(0); }
GMB(steam_file_write_file) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(0); }

/* ---------------- ini ---------------- */
typedef struct { char *sec, *key, *val; } ini_ent;
static ini_ent *ent;
static int nent, capent;
static char ini_name[96];
static int ini_dirty, ini_is_open;

static void ini_clear(void)
{
    for (int k = 0; k < nent; k++) { free(ent[k].sec); free(ent[k].key); free(ent[k].val); }
    nent = 0;
}
static ini_ent *ini_find(const char *sec, const char *key)
{
    for (int k = 0; k < nent; k++) if (!strcmp(ent[k].sec, sec) && !strcmp(ent[k].key, key)) return &ent[k];
    return NULL;
}
static void ini_put(const char *sec, const char *key, const char *val)
{
    ini_ent *e = ini_find(sec, key);
    if (e) { if (strcmp(e->val, val)) { free(e->val); e->val = strdup(val); ini_dirty = 1; } return; }
    {
        if (nent == capent) { capent = capent ? capent * 2 : 64; ent = realloc(ent, capent * sizeof *ent); }
        ent[nent].sec = strdup(sec); ent[nent].key = strdup(key); ent[nent].val = strdup(val); nent++;
    }
    ini_dirty = 1;
}
GMB(ini_open)
{
    (void)self; (void)other;
    char t[96], line[1024], sec[256] = "";
    snprintf(ini_name, sizeof ini_name, "%s", argstr(GM_ARG(0), t, sizeof t));
    ini_is_open = 1;
    if (ini_cache_ok && !strcmp(ini_name, ini_cache_name)) { ini_dirty = 0; return GM_UNDEF_V; }
    ini_clear();
    ini_dirty = 0;
    snprintf(ini_cache_name, sizeof ini_cache_name, "%s", ini_name);
    int len;
    char *data = fs_read(ini_name, &len);
    if (!data) { ini_cache_ok = 1; return GM_UNDEF_V; }
    for (int pos = 0; pos < len;) {
        int n = 0;
        while (pos < len && data[pos] != '\n') { if (n < (int)sizeof line - 1) line[n++] = data[pos]; pos++; }
        pos++;
        while (n && (line[n - 1] == '\r')) n--;
        line[n] = 0;
        if (line[0] == '[') { char *e = strchr(line, ']'); if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", line + 1); } continue; }
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        n = (int)strlen(v);
        if (n >= 2 && v[0] == '"' && v[n - 1] == '"') { v[n - 1] = 0; v++; }
        ini_put(sec, line, v);
    }
    free(data);
    ini_dirty = 0; ini_cache_ok = 1;
    return GM_UNDEF_V;
}
GMB(ini_close)
{
    (void)self; (void)other; (void)argc; (void)argv;
    if (ini_is_open && ini_dirty) {
        int cap = 4096, n = 0;
        char *out = malloc(cap);
        for (int k = 0; k < nent; k++) {
            int first = 1;
            for (int j = 0; j < k; j++) if (!strcmp(ent[j].sec, ent[k].sec)) { first = 0; break; }
            if (!first) continue;
            for (int pass = -1; pass < nent; pass++) {
                int j = pass < 0 ? -1 : pass;
                if (pass >= 0 && (pass < k || strcmp(ent[j].sec, ent[k].sec))) continue;
                int need = pass < 0 ? (int)strlen(ent[k].sec) + 8 : (int)(strlen(ent[j].key) + strlen(ent[j].val)) + 8;
                if (n + need > cap) { cap = (n + need) * 2; out = realloc(out, cap); }
                n += pass < 0 ? sprintf(out + n, "[%s]\r\n", ent[k].sec) : sprintf(out + n, "%s=\"%s\"\r\n", ent[j].key, ent[j].val);
            }
        }
        fs_write(ini_name, out, n);
        free(out);
        ini_cache_ok = 1;
    }
    ini_is_open = 0; ini_dirty = 0;
    return GM_UNDEF_V;
}
GMB(ini_section_exists)
{
    (void)self; (void)other;
    char t1[64]; const char *s = gm_cstr(GM_ARG(0), t1, sizeof t1);
    for (int k = 0; k < nent; k++) if (!strcmp(ent[k].sec, s)) return gm_real(1);
    return gm_real(0);
}
GMB(ini_read_real)
{
    (void)self; (void)other;
    char t1[64], t2[64];
    ini_ent *e = ini_find(gm_cstr(GM_ARG(0), t1, sizeof t1), gm_cstr(GM_ARG(1), t2, sizeof t2));
    return e ? gm_real((gm_real_t)atof(e->val)) : gm_real(A_REAL(2));
}
GMB(ini_read_string)
{
    (void)self; (void)other;
    char t1[64], t2[64], t3[64];
    ini_ent *e = ini_find(gm_cstr(GM_ARG(0), t1, sizeof t1), gm_cstr(GM_ARG(1), t2, sizeof t2));
    if (e) return gm_str_make(e->val, (int)strlen(e->val));
    const char *d = gm_cstr(GM_ARG(2), t3, sizeof t3);
    return gm_str_make(d, (int)strlen(d));
}
GMB(ini_write_real)
{
    (void)self; (void)other;
    char t1[64], t2[64], t3[64];
    const char *v = gm_cstr(gm_conv_s(GM_ARG(2)), t3, sizeof t3);
    ini_put(gm_cstr(GM_ARG(0), t1, sizeof t1), gm_cstr(GM_ARG(1), t2, sizeof t2), v);
    return GM_UNDEF_V;
}
GMB(ini_write_string)
{
    (void)self; (void)other;
    char t1[64], t2[64], t3[64];
    ini_put(gm_cstr(GM_ARG(0), t1, sizeof t1), gm_cstr(GM_ARG(1), t2, sizeof t2), gm_cstr(GM_ARG(2), t3, sizeof t3));
    return GM_UNDEF_V;
}
