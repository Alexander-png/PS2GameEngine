#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "ge/log.h"
#include "ge/io.h"

static char *buf; static int cap, len, truncated;
static char path[96], base[96];

void ge_log_init(const char *app, int capacity)
{
    char ts[24];
    free(buf);
    buf = malloc(capacity); cap = capacity; len = 0; truncated = 0;
    if (ge_io_timestamp(ts) == 0) snprintf(path, sizeof path, "logs/%s-%s.txt", app, ts);
    else {          /* no usable console clock (e.g. dead RTC battery): number the runs; uploaded logs live on as .sent */
        for (int n = 1; n < 1000; n++) {
            char sent[100];
            snprintf(path, sizeof path, "logs/%s-r%03d.txt", app, n);
            snprintf(sent, sizeof sent, "%s.sent", path);
            if (!ge_io_exists(path) && !ge_io_exists(sent)) break;
        }
    }
    strcpy(base, path);
    ge_log("log start: %s, io source %s", path, ge_io_source());
}

void ge_log(const char *fmt, ...)
{
    if (!buf) return;
    char line[320];
    unsigned ms = ge_time_ms();
    int n = snprintf(line, sizeof line, "[%4u.%03u] ", ms / 1000, ms % 1000);
    va_list ap; va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = sizeof line - 2;
    line[n++] = '\n';
#ifdef GE_LOG_STDOUT          /* emulator runs: the EE console ends up in PCSX2's emulog.txt */
    fwrite(line, 1, n, stdout); fflush(stdout);
#endif
    if (len + n > cap - 32) { truncated = 1; return; }
    memcpy(buf + len, line, n); len += n;
}

int ge_log_save(void)
{
    if (!buf) return -1;
    int keep = len;
    if (truncated) { const char *m = "[log truncated]\n"; int k = strlen(m); memcpy(buf + len, m, k); len += k; }   /* tail is reserved */
    int r = ge_io_write(path, buf, len);
    len = keep;
    return r;
}
const char *ge_log_name(void) { return path; }
int ge_log_truncated(void) { return truncated; }
int ge_log_size(void) { return len; }
void ge_log_segment(const char *tag)
{
    if (!buf) return;
    char old[96]; strcpy(old, path);
    ge_log("log continues in segment '%s'", tag);
    ge_log_save();
    int dot = strlen(base) - 4;                     /* ".txt" */
    snprintf(path, sizeof path, "%.*s-%s.txt", dot, base, tag);
    len = 0; truncated = 0;
    ge_log("log segment '%s' (continues %s)", tag, old);
}
