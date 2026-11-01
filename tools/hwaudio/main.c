// hwaudio: sound test for the real console. Files come from the HDD (hdd0:+DEVGAME /current), no IOP reset (the launcher's
// dev9/atad stay up). engsnd (IOP sound server over freesd) streams music through iomanX from pfs0:, SFX are uploaded by the EE.
// Phases (screen shows which one is running, log has stream counters + measured playback speed vs expected):
//   1 SFX one-shots + pitch   2 music stream with pitch changes   3 crossfade   4 prepared switch + loop + SFX over music
//   5 panning (mono stream sweep)      START aborts the test, any of START/CROSS/CIRCLE (or timeout) leaves the final screen.
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <debug.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sbv_patches.h>
#include "ge/io.h"
#include "ge/log.h"
#include "ge/input.h"

#ifndef EXIT_SECS
#define EXIT_SECS 60
#endif
extern unsigned char freesd_irx[]; extern unsigned int size_freesd_irx;
extern unsigned char engsnd_x_irx[]; extern unsigned int size_engsnd_x_irx;

#define DEV 0x0E460001
enum { C_INIT = 1, C_UPLOAD, C_VOICE, C_KON, C_KOFF, C_PITCH, C_STATUS, C_VOL, C_SSTART, C_SSTOP, C_SSTAT, C_SPLAY, C_SVOL, C_VPAN };
#define OUT(...) do { scr_printf(__VA_ARGS__); ge_log(__VA_ARGS__); } while (0)

static SifRpcClientData_t cd;
static int *buf;
static int aborted;

static int call(int cmd, int n) { SifCallRpc(&cd, cmd, 0, buf, n, buf, 4 * 32, NULL, NULL); return buf[0]; }
static void upload(const unsigned char *p, int len, int spu)
{
    for (int o = 0; o < len; o += 0xE000) {
        int n = len - o > 0xE000 ? 0xE000 : len - o;
        buf[0] = spu + o; buf[1] = n;
        memcpy((unsigned char *)buf + 64, p + o, n);
        SifCallRpc(&cd, C_UPLOAD, 0, buf, 64 + n, buf, 16, NULL, NULL);
    }
}
static void voice(int core, int v, int ssa, int pitch, int vl, int vr)
{ int a[8] = {core, v, ssa, pitch, vl, vr, 0x00FF, 0x1FC0}; memcpy(buf, a, sizeof a); call(C_VOICE, sizeof a); }
static void kon(int core, int mask) { buf[0] = core; buf[1] = mask; call(C_KON, 8); }
static void koff(int core, int mask) { buf[0] = core; buf[1] = mask; call(C_KOFF, 8); }
static void setpitch(int core, int v, int p) { buf[0] = core; buf[1] = v; buf[2] = p; call(C_PITCH, 12); }
static int sstat(int slot, int *o) { buf[0] = slot; SifCallRpc(&cd, C_SSTAT, 0, buf, 4, buf, 64, NULL, NULL); memcpy(o, buf, 44); return o[0]; }
static void sstart2(int slot, int vl, int vr, int ringl, int ringr, int pitch, int vol, const char *name, int autoplay)
{
    char path[96]; snprintf(path, sizeof path, "pfs0:/current/%s", name);
    int a[10] = {slot, 1, vl, vr, ringl, ringr, pitch, vol, 0, autoplay};
    memcpy(buf, a, sizeof a);
    strcpy((char *)buf + 64, path);
    call(C_SSTART, 64 + strlen(path) + 1);
    int ss[11]; unsigned t0 = ge_time_ms();
    for (int i = 0; i < 600; i++) { if (sstat(slot, ss) == (autoplay ? 2 : 5) || ss[0] == 4) break; usleep(5000); }
    OUT("  slot%d %s %s: %u ms, state=%d err=%d\n", slot, autoplay ? "start" : "prep ", name, ge_time_ms() - t0, ss[0], ss[3]);
}
static void sstart(int slot, int vl, int vr, int rl, int rr, int pitch, int vol, const char *name) { sstart2(slot, vl, vr, rl, rr, pitch, vol, name, 1); }
static void splay(int slot) { buf[0] = slot; call(C_SPLAY, 4); }
static void sstop(int slot) { buf[0] = slot; call(C_SSTOP, 4); usleep(30000); }
static void spanvol(int slot, int vol, int pan) { buf[0] = slot; buf[1] = vol; buf[2] = pan; call(C_SVOL, 12); }

/* wait that returns early (and sets `aborted`) when START is pressed */
static void wait_ms(int ms)
{
    unsigned end = ge_time_ms() + ms;
    while (!aborted && ge_time_ms() < end) { if (ge_input_poll() & GE_BTN_START) aborted = 1; usleep(10000); }
}

#define BASE (4096 * 44100 / 48000)
#define SFXBASE (4096 * 22050 / 48000)
#define V 0x3000

static int sfx_spu[16], sfx_n;
static int load_sfx(const char *name)
{
    int size; unsigned char *d = ge_io_read_all(name, &size);
    if (!d || size <= 48) { OUT("  missing %s\n", name); return -1; }
    static int at = 0x5010;
    int len = size - 48;
    upload(d + 48, len, at);
    sfx_spu[sfx_n] = at;
    OUT("  sfx %-18s spu 0x%06X len %d\n", name, at, len);
    at += (len + 15) & ~15;
    ge_io_free(d);
    return sfx_n++;
}
static int next_v = 10;
static void play_sfx(int i, float ratio)
{
    int v = next_v; next_v = next_v == 15 ? 10 : next_v + 1;
    voice(1, v, sfx_spu[i], (int)(SFXBASE * ratio), 0x3FFF, 0x3FFF);
    kon(1, 1 << v);
}

/* measure played speed of a running stream vs the expected ratio: ADPCM blocks the voice played from the live NAX */
static void measure(int slot, int ms, float ratio, const char *what)
{
    int s0[11], s1[11];
    sstat(slot, s0); unsigned t0 = ge_time_ms();
    wait_ms(ms);
    sstat(slot, s1); unsigned t1 = ge_time_ms();
    double rate = (double)(s1[6] - s0[6]) * 28.0 / ((t1 - t0) / 1000.0);   /* samples per second played */
    OUT("  %s: %.1f%% of expected (halves=%d under=%d err=%d | refill max %d us, >100ms %d, short %d)\n", what, rate / (44100.0 * ratio) * 100.0, s1[1], s1[2], s1[3], s1[7], s1[8], s1[9]);
}
static void stats(int slot, const char *what)
{
    int ss[11]; sstat(slot, ss);
    OUT("  %s: halves=%d under=%d err=%d | refill max %d us, >100ms %d, short reads %d, loop wraps %d\n", what, ss[1], ss[2], ss[3], ss[7], ss[8], ss[9], ss[10]);
}

static void phases(void)
{
    int ss[11];
    scr_clear();
    OUT("PHASE 1: SFX (select, save, bell, txttor, hurt) then bell at x0.5 / x1.5\n");
    const char *sf[] = {"snd_select.vag", "snd_save.vag", "snd_bell.vag", "snd_txttor.vag", "snd_hurt1.vag"};
    int id[5];
    for (int i = 0; i < 5; i++) id[i] = load_sfx(sf[i]);
    for (int i = 0; i < 5 && !aborted; i++) { if (id[i] >= 0) play_sfx(id[i], 1.0f); wait_ms(1300); }
    if (id[2] >= 0) { play_sfx(id[2], 0.5f); wait_ms(1800); play_sfx(id[2], 1.5f); wait_ms(1200); }
    if (aborted) return;

    scr_clear();
    OUT("PHASE 2: music stream mus_xpart: x1 / x0.5 / x0.25 / x1.5\n");
    sstart(0, 0, 1, 0x100000, 0x110000, BASE, V, "XPART.VAS");
    measure(0, 5000, 1.0f, "x1.0 ");
    setpitch(1, 0, BASE / 2); setpitch(1, 1, BASE / 2);   measure(0, 4000, 0.5f, "x0.5 ");
    setpitch(1, 0, BASE / 4); setpitch(1, 1, BASE / 4);   measure(0, 4000, 0.25f, "x0.25");
    setpitch(1, 0, BASE * 3 / 2); setpitch(1, 1, BASE * 3 / 2); measure(0, 4000, 1.5f, "x1.5 ");
    if (aborted) { sstop(0); return; }

    OUT("PHASE 3: crossfade xpart -> menu0 (2 s)\n");
    setpitch(1, 0, BASE); setpitch(1, 1, BASE);
    sstart(1, 6, 7, 0x120000, 0x130000, BASE, 0, "MENU0.VAS");
    for (int v = 0; v <= 20 && !aborted; v++) { spanvol(0, V * (20 - v) / 20, 0); spanvol(1, V * v / 20, 0); wait_ms(100); }
    stats(0, "slot0 (xpart)");
    sstop(0);
    wait_ms(3000);
    stats(1, "slot1 (menu0)");
    if (aborted) { sstop(1); return; }

    scr_clear();
    OUT("PHASE 4: prepared switch to zzz_c (8 s loop, ~3 loops) with a bell every 2.5 s over it\n");
    sstart2(0, 0, 1, 0x100000, 0x110000, BASE, V, "ZZZC.VAS", 0);
    wait_ms(300);
    splay(0); sstop(1);
    for (int k = 0; k < 8 && !aborted; k++) {
        if (id[2] >= 0) play_sfx(id[2], 1.0f);
        wait_ms(2500);
        sstat(0, ss); OUT("  t+%.1fs: halves=%d wraps=%d refill max %d us, refills >100ms %d, short reads %d\n", (k + 1) * 2.5, ss[1], ss[10], ss[7], ss[8], ss[9]);
    }
    stats(0, "slot0 (zzz_c)");
    sstop(0);
    wait_ms(800);
    if (aborted) return;

    scr_clear();
    OUT("PHASE 5: pan, mono stream cyber_battle_backing: L->R->L (listen for the sound moving)\n");
    sstart(0, 0, 1, 0x100000, 0x110000, BASE, V, "CBACK.VAS");
    wait_ms(2000);
    for (int i = 0; i <= 40 && !aborted; i++) { spanvol(0, V, -4096 + 8192 * i / 40); wait_ms(100); }
    for (int i = 0; i <= 40 && !aborted; i++) { spanvol(0, V, 4096 - 8192 * i / 40); wait_ms(100); }
    stats(0, "slot0 (cback)");
    sstop(0);
    (void)ss;
}

int main(void)
{
    init_scr();
    ge_log_init("hwaudio", 64 * 1024);
    int io_r = ge_io_init("hdd0:+DEVGAME", "/current");
    char info[160]; ge_io_sysinfo(info, sizeof info);
    OUT("hwaudio: storage init %d (%s); %s\n", io_r, ge_io_source(), info);
    if (io_r < 0) { ge_log_save(); sleep(5); return 1; }

    sbv_patch_enable_lmb();
    int m1 = 0, m2 = 0;
    int r1 = SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &m1);
    int r2 = SifExecModuleBuffer(engsnd_x_irx, size_engsnd_x_irx, 0, NULL, &m2);
    OUT("freesd id=%d res=%d  engsnd id=%d res=%d\n", r1, m1, r2, m2);
    for (int t = 0; t < 200; t++) { if (SifBindRpc(&cd, DEV, 0) >= 0 && cd.server) break; usleep(20000); }
    OUT("engsnd bind server=%p\n", cd.server);
    int pad_r = ge_input_init();
    OUT("pad init %d\n", pad_r);
    if (cd.server) {
        buf = memalign(64, 0x10000);
        OUT("engsnd init=%d\n", call(C_INIT, 4));
        phases();
        sstop(0); sstop(1);
        koff(1, 0xFFFFFF);
        OUT(aborted ? "aborted by START\n" : "test finished\n");
    } else OUT("no engsnd server\n");
    ge_log_save();

    scr_printf("\nlog %s saved. Press START, CROSS or CIRCLE to exit (auto-exit in %d s)\n", ge_log_name(), EXIT_SECS);
    unsigned t_end = ge_time_ms() + EXIT_SECS * 1000;
    while (ge_input_poll()) usleep(20000);          /* wait for the buttons used during the test to be released */
    while (ge_time_ms() < t_end && !(ge_input_poll() & (GE_BTN_START | GE_BTN_CROSS | GE_BTN_CIRCLE))) usleep(20000);
    ge_log("exit: %s", ge_time_ms() >= t_end ? "timeout" : "button");
    ge_log_save();
    ge_input_shutdown();
    ge_io_shutdown();
    scr_printf("bye\n");
    return 0;
}
