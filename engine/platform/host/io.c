/* Host (Linux) file access for the test harness: names are relative to the directory given to ge_io_init (default "."). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "ge/io.h"

struct ge_file { FILE *f; int size; };
static char base[256] = "";
static void path_of(const char *name, char *out, int n) { snprintf(out, n, "%s%s", base, name); }

int ge_io_init(const char *part, const char *dir) { (void)part; snprintf(base, sizeof base, "%s", dir ? dir : ""); return 0; }
const char *ge_io_source(void) { return "host"; }
void *ge_io_alloc(int size) { return aligned_alloc(64, ((size_t)size + 63) & ~(size_t)63 ? ((size_t)size + 63) & ~(size_t)63 : 64); }
void ge_io_free(void *p) { free(p); }
ge_file_t *ge_io_open(const char *name)
{
    char p[512]; path_of(name, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    ge_file_t *g = malloc(sizeof *g);
    g->f = f;
    fseek(f, 0, SEEK_END); g->size = (int)ftell(f);
    return g;
}
int ge_io_pread(ge_file_t *f, unsigned off, void *buf, int len)
{
    if (fseek(f->f, off, SEEK_SET)) return -1;
    return fread(buf, 1, len, f->f) == (size_t)len || feof(f->f) ? len : -1;
}
void ge_io_close(ge_file_t *f) { fclose(f->f); free(f); }
int ge_io_size(ge_file_t *f) { return f->size; }
void *ge_io_read_all(const char *name, int *size)
{
    ge_file_t *f = ge_io_open(name);
    if (!f) return NULL;
    void *b = ge_io_alloc(f->size + 1);
    int n = ge_io_pread(f, 0, b, f->size);
    int sz = f->size;
    ge_io_close(f);
    if (n < 0) { free(b); return NULL; }
    if (size) *size = sz;
    return b;
}
int ge_io_write(const char *name, const void *data, int len)
{
    char p[512], q[520]; path_of(name, p, sizeof p);
    for (char *s = p; (s = strchr(s + 1, '/')); ) { *s = 0; mkdir(p, 0755); *s = '/'; }
    snprintf(q, sizeof q, "%s.part", p);
    FILE *f = fopen(q, "wb");
    if (!f) return -1;
    fwrite(data, 1, len, f);
    fclose(f);
    return rename(q, p);
}
int ge_io_exists(const char *name) { ge_file_t *f = ge_io_open(name); if (!f) return 0; ge_io_close(f); return 1; }
int ge_io_timestamp(char out[24]) { time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(out, 24, "%Y%m%d-%H%M%S", &tm); return 0; }
unsigned ge_time_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (unsigned)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u); }
void ge_io_shutdown(void) {}
void ge_io_sysinfo(char *out, int cap) { snprintf(out, cap, "host"); }
