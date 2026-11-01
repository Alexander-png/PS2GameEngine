/* PS2 file access. Disc (cdrom0) or the internal HDD (PFS partition, e.g. hdd0:+DEVGAME deployed by PS2Deployer).
 * HDD mode needs the HDD drivers (ps2dev9/ps2atad) to be up already, as when started from uLaunchELF; this module loads
 * fileXio, ps2hdd and ps2fs itself (same arguments as PS2Deployer, which is proven on hardware). */
#define NEWLIB_PORT_AWARE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <malloc.h>
#include <kernel.h>
#include <libcdvd.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include "ge/io.h"

extern unsigned char fileXio_irx[], ps2hdd_irx[], ps2fs_irx[];
extern unsigned int size_fileXio_irx, size_ps2hdd_irx, size_ps2fs_irx;
static const char ps2hdd_args[] = "-o\0" "4\0" "-n\0" "20\0";
static const char ps2fs_args[] = "-m\0" "4\0" "-o\0" "10\0" "-n\0" "40\0";

struct ge_file { int fd; };
static int inited, use_hdd;
static char hdd_dir[96];

static void init(void)
{
    if (inited) return;
    SifInitRpc(0); sceCdInit(SCECdINIT);
    for (int i = 0; i < 200 && sceCdDiskReady(0) == SCECdNotReady; i++) usleep(50000);
    inited = 1;
}
unsigned ge_time_ms(void) { return (unsigned)((unsigned long long)clock() * 1000ull / CLOCKS_PER_SEC); }
const char *ge_io_source(void) { return use_hdd ? "hdd" : "disc"; }

static int load_irx(void *img, unsigned size, const char *args, int arglen)
{
    int modres = 0, r = SifExecModuleBuffer(img, size, arglen, (void *)args, &modres);
    return r < 0 ? r : modres;
}
int ge_io_init(const char *part, const char *dir)
{
    init();
    if (!part) return 0;
    if (load_irx(fileXio_irx, size_fileXio_irx, NULL, 0) < 0) return -1;
    if (load_irx(ps2hdd_irx, size_ps2hdd_irx, ps2hdd_args, sizeof ps2hdd_args) < 0) return -2;
    if (load_irx(ps2fs_irx, size_ps2fs_irx, ps2fs_args, sizeof ps2fs_args) < 0) return -3;
    if (fileXioInit() < 0) return -4;
    if (fileXioMount("pfs0:", part, 0) < 0) return -5;
    snprintf(hdd_dir, sizeof hdd_dir, "%s", dir ? dir : "");
    use_hdd = 1;
    return 0;
}
int ge_io_timestamp(char out[24])
{
    init();
    sceCdCLOCK c;
    if (sceCdReadClock(&c) && !c.stat) {                    /* values are BCD */
        #define BCD(v) (((v) >> 4) * 10 + ((v) & 15))
        snprintf(out, 24, "20%02d%02d%02d-%02d%02d%02d", BCD(c.year), BCD(c.month & 0x1F), BCD(c.day), BCD(c.hour), BCD(c.minute), BCD(c.second));
        return 0;
    }
    snprintf(out, 24, "t%u", ge_time_ms());
    return -1;
}

int ge_io_exists(const char *name)
{
    ge_file_t *f = ge_io_open(name);
    if (!f) return 0;
    ge_io_close(f);
    return 1;
}

void *ge_io_alloc(int size) { return memalign(64, (size + 63) & ~63); }
void ge_io_free(void *p) { free(p); }

ge_file_t *ge_io_open(const char *name)
{
    init();
    char p[192];
    if (use_hdd) snprintf(p, sizeof p, "pfs0:%s/%s", hdd_dir, name);
    else snprintf(p, sizeof p, "cdrom0:\\%s;1", name);
    int fd = open(p, O_RDONLY);
    if (fd < 0) return NULL;
    ge_file_t *f = malloc(sizeof *f); f->fd = fd;
    return f;
}
int ge_io_pread(ge_file_t *f, unsigned off, void *buf, int len)
{
    if (lseek(f->fd, off, SEEK_SET) < 0) return -1;
    int got = 0;
    while (got < len) { int n = read(f->fd, (char *)buf + got, len - got > 65536 ? 65536 : len - got); if (n <= 0) break; got += n; }
    return got == len ? got : -1;
}
void ge_io_close(ge_file_t *f) { if (f) { close(f->fd); free(f); } }

void *ge_io_read_all(const char *name, int *size)
{
    ge_file_t *f = ge_io_open(name);
    if (!f) return NULL;
    int sz = lseek(f->fd, 0, SEEK_END);
    void *b = ge_io_alloc(sz);
    if (ge_io_pread(f, 0, b, sz) < 0) { ge_io_free(b); b = NULL; }
    ge_io_close(f);
    if (b) *size = sz;
    return b;
}

int ge_io_write(const char *name, const void *data, int len)
{
    if (!use_hdd) return -1;
    char path[192], part[200];
    int n = snprintf(path, sizeof path, "pfs0:%s/%s", hdd_dir, name);
    if (n >= (int)sizeof path - 8) return -2;
    for (char *s = path + 5 + strlen(hdd_dir) + 1; *s; s++)           /* create folders below the game dir */
        if (*s == '/') { *s = 0; mkdir(path, 0777); *s = '/'; }
    snprintf(part, sizeof part, "%s.part", path);
    int fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -3;
    const char *p = data; int left = len;
    while (left > 0) { int w = write(fd, p, left); if (w <= 0) { close(fd); unlink(part); return -4; } p += w; left -= w; }
    if (close(fd) < 0) { unlink(part); return -5; }
    unlink(path);                                                      /* rename onto an existing file is not guaranteed */
    return fileXioRename(part, path) < 0 ? -6 : 0;
}

int ge_io_size(ge_file_t *f) { return (int)lseek(f->fd, 0, SEEK_END); }

void ge_io_shutdown(void)
{
    if (use_hdd) { fileXioUmount("pfs0:"); use_hdd = 0; }
    fileXioExit();
}

void ge_io_sysinfo(char *out, int cap)
{
    char rom[24] = "?";
    int fd = open("rom0:ROMVER", O_RDONLY);
    if (fd >= 0) { int n = read(fd, rom, sizeof rom - 1); close(fd); if (n > 0) rom[n] = 0; for (char *c = rom; *c; c++) if (*c < 32) *c = 0; }
    int n = snprintf(out, cap, "ROMVER %s", rom);
    if (use_hdd) {
        int total = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
        int fre = fileXioDevctl("hdd0:", HDIOC_FREESECTOR, NULL, 0, NULL, 0);
        if (total > 0) n += snprintf(out + n, cap - n, ", hdd0 %d MB total (%d sectors), %d MB free", (int)((long long)total * 512 / 1048576), total, fre > 0 ? (int)((long long)fre * 512 / 1048576) : -1);
    }
}
