// roomtest: engine smoke test over a .gmpk. Default: visits every room (load blob, upload, draw 2 frames, free) and reports
// timings/failures. -DSHOWROOM=\"room_name\" shows one room scrolling instead.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <debug.h>
#include <graph.h>
#include "ge/gfx.h"
#include "ge/io.h"
#include "ge/log.h"
#include "ge/input.h"
#include "ge/room.h"

/* raw storage speed of GAME.GMP: sequential 256 KB reads (up to 32 MB) and 200 random 64 KB reads */
#ifndef EXIT_SECS
#define EXIT_SECS 60
#endif
static unsigned us(void);
static void io_bench(void)
{
    ge_file_t *f = ge_io_open("GAME.GMP");
    if (!f) { ge_log("iobench: cannot open GAME.GMP"); return; }
    int size = ge_io_size(f);
    char *buf = ge_io_alloc(256 * 1024);
    int total = size < 32 * 1024 * 1024 ? size : 32 * 1024 * 1024;
    total &= ~(256 * 1024 - 1);
    unsigned t0 = us(); int got = 0;
    for (int off = 0; off + 256 * 1024 <= total; off += 256 * 1024) { if (ge_io_pread(f, off, buf, 256 * 1024) < 0) break; got += 256 * 1024; }
    unsigned t = us() - t0;
    if (t) ge_log("iobench: sequential %d KB in %u ms = %u KB/s", got / 1024, t / 1000, (unsigned)((unsigned long long)got * 1000000ull / t / 1024));
    unsigned seed = 12345, tmax = 0, tsum = 0; int n = 0;
    for (int i = 0; i < 200 && size > 65536 * 2; i++) {
        seed = seed * 1103515245u + 12345u;
        int off = (int)((seed >> 8) % (unsigned)(size - 65536)) & ~2047;
        unsigned a = us();
        if (ge_io_pread(f, off, buf, 65536) < 0) break;
        unsigned d = us() - a; tsum += d; if (d > tmax) tmax = d; n++;
    }
    if (n) ge_log("iobench: %d random 64 KB reads: avg %u us, max %u us", n, tsum / n, tmax);
    ge_io_free(buf); ge_io_close(f);
}
static unsigned us(void) { return (unsigned)((unsigned long long)clock() * 1000000ull / CLOCKS_PER_SEC); }
int main(void)
{
#ifdef HDD_BOOT
    int io_r = ge_io_init("hdd0:+DEVGAME", "/current");
#else
    int io_r = ge_io_init(NULL, NULL);
#endif
    ge_log_init("roomtest", 256 * 1024);
    ge_log("storage init result %d, source %s", io_r, ge_io_source());
    { char si[160]; ge_io_sysinfo(si, sizeof si); ge_log("system: %s", si); }
    int pad_r = ge_input_init();
    ge_log("pad init result %d", pad_r);
    ge_gfx_init(640, 480);
    ge_pack_t *p = ge_pack_open("GAME.GMP");
    if (!p) { ge_log("pack open failed"); ge_log_save(); init_scr(); scr_printf("pack open failed\n"); for (;;) sleep(1); }
    ge_log("pack GAME.GMP: %d rooms", ge_pack_room_count(p));
#ifndef SHOWROOM
    io_bench();
#endif
    int n = ge_pack_room_count(p);
#ifdef SHOWROOM
#ifdef FROMROOM
    ge_room_t *r0 = ge_room_load_index(p, ge_pack_find(p, FROMROOM));
    ge_room_prefetch(r0, 0, 0); ge_room_draw(r0, 0, 0);
#endif
    ge_room_t *r = ge_room_load_index(p, ge_pack_find(p, SHOWROOM));
#ifdef FROMROOM
    ge_room_prefetch(r, 0, 0); ge_room_free(r0);
#endif
    if (!r) { init_scr(); scr_printf("room load failed\n"); for (;;) sleep(1); }
    int w, h; ge_room_size(r, &w, &h);
    int maxcam = h > 240 ? h - 240 : 0;
#ifdef CAMX
    for (int acc = 0;;) { graph_wait_vsync(); if ((acc += ge_room_speed(r)) >= 60) { acc -= 60; ge_room_step(r); } ge_room_draw(r, CAMX, CAMY); }
#endif
    for (int f = 0;; f++) {
        int ph = f % 300, cy;
        if (ph < 60) cy = 0; else if (ph < 150) cy = maxcam * (ph - 60) / 90; else if (ph < 210) cy = maxcam; else cy = maxcam - maxcam * (ph - 210) / 90;
        graph_wait_vsync();
        { static int acc; if ((acc += ge_room_speed(r)) >= 60) { acc -= 60; ge_room_step(r); } }
        ge_room_draw(r, 0, cy);
    }
#else
    unsigned tmax = 0, tsum = 0; int fail = 0, miss = 0, worst = -1, nitems = 0, maxitems = 0;
    ge_room_t *prev = NULL;
    unsigned dsum = 0, dmax = 0; int dworst = -1;
    for (int i = 0; i < n; i++) {                 /* new room is loaded before the previous one is freed, like a real transition */
        unsigned t0 = us();
        ge_room_t *r = ge_room_load_index(p, i);
        unsigned t_load = us() - t0;
        if (!r) { fail++; ge_log("room %d %s: LOAD FAILED", i, ge_pack_room_name(p, i)); continue; }
        int a, it; ge_room_stats(r, &a, &it);
        miss += ge_room_missing_tex(r) != 0;
        nitems += it; if (it > maxitems) maxitems = it;
        unsigned t1 = us();
#ifdef PREFETCH
        ge_room_prefetch(r, 0, 0);
#endif
        unsigned t_pre = us() - t1;
        unsigned t2 = us();
        for (int f = 0; f < 2; f++) { if (f == 1) t2 = us(); ge_room_draw(r, 0, 0); }
        unsigned t_draw = us() - t2;               /* second frame: textures resident, includes GS finish wait */
        ge_log("room %3d %-28s items %4d assets %3d load %6u us prefetch %7u us draw %6u us", i, ge_pack_room_name(p, i), it, a, t_load, t_pre, t_draw);
        ge_room_free(prev); prev = r;
        t_load += t_pre;
        tsum += t_load; if (t_load > tmax) { tmax = t_load; worst = i; }
        dsum += t_draw; if (t_draw > dmax) { dmax = t_draw; dworst = i; }
        if (i % 40 == 39) ge_log_save();
    }
    ge_room_free(prev);
    ge_log("summary: %d rooms, %d failures, %d rooms with textures that do not fit VRAM", n, fail, miss);
    ge_log("load+prefetch avg %u us, max %u us (%s); draw avg %u us, max %u us (%s); items avg %d max %d",
           tsum / n, tmax, worst >= 0 ? ge_pack_room_name(p, worst) : "-", dsum / n, dmax, dworst >= 0 ? ge_pack_room_name(p, dworst) : "-", nitems / n, maxitems);
    { unsigned u, e, b, h; ge_gfx_stats(&u, &e, &b, &h); ge_log("textures: %u uploads, %u evictions, %u KB uploaded, %u cache hits", u, e, b / 1024, h); }
    int save_r = ge_log_save();
    init_scr();
    scr_printf("log %s: save result %d%s\n", ge_log_name(), save_r, save_r < 0 ? " (not saved: no HDD)" : "");
    scr_printf("pack: %d rooms visited, %d load failures, %d rooms with textures that did not fit VRAM\n", n, fail, miss);
    scr_printf("load+upload avg %u us, max %u us (room %s); items avg %d max %d\n", tsum / n, tmax, worst >= 0 ? ge_pack_room_name(p, worst) : "-", nitems / n, maxitems);
    { unsigned u, e, b, h; ge_gfx_stats(&u, &e, &b, &h); scr_printf("textures: %u uploads, %u evictions, %u KB uploaded, %u cache hits\n", u, e, b / 1024, h); }
    {   /* leave on START/CROSS/CIRCLE or after EXIT_SECS (default 60) */
        unsigned t_end = ge_time_ms() + EXIT_SECS * 1000u; int shown = -1; unsigned btn = 0;
        scr_printf("\nPress START, CROSS or CIRCLE to exit (auto-exit in %d s)\n", EXIT_SECS);
        while (ge_time_ms() < t_end) {
            btn = ge_input_poll();
            if (btn & (GE_BTN_START | GE_BTN_CROSS | GE_BTN_CIRCLE)) break;
            int left = (int)((t_end - ge_time_ms()) / 1000);
            if (left != shown) { shown = left; scr_printf("\r%3d s ", left); }
            usleep(20000);
        }
        ge_log("exit: %s", (btn & (GE_BTN_START | GE_BTN_CROSS | GE_BTN_CIRCLE)) ? "button" : "timeout");
        ge_log_save();
        ge_input_shutdown();
        ge_io_shutdown();
        scr_printf("\nbye\n");
        return 0;
    }
#endif
}
