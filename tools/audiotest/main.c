// audiotest: EE side of the engsnd IOP sound server test. Uploads converted VAG (SFX + a music track split L/R) to SPU2 RAM,
// plays them, sweeps pitch (UT-style slowdowns 1.0 -> 0.5 -> 0.25 -> 1.5) and reports measured NAX speed vs expected.
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <debug.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sbv_patches.h>
#include <libcdvd.h>

extern unsigned char freesd_irx[]; extern unsigned int size_freesd_irx;
extern unsigned char engsnd_irx[]; extern unsigned int size_engsnd_irx;
extern unsigned char testaudio_bin[]; extern unsigned int size_testaudio_bin;
extern unsigned char panaudio_bin[]; extern unsigned int size_panaudio_bin;

#define DEV 0x0E460001
enum { C_INIT = 1, C_UPLOAD, C_VOICE, C_KON, C_KOFF, C_PITCH, C_STATUS, C_VOL, C_SSTART, C_SSTOP, C_SSTAT, C_SPLAY, C_SVOL, C_VPAN };

static unsigned now_ms(void);
#define LOG(...) do { scr_printf(__VA_ARGS__); } while (0)
static SifRpcClientData_t cd;
static int *buf;

static int call(int cmd, int sendbytes)
{
    SifCallRpc(&cd, cmd, 0, buf, sendbytes, buf, 4 * 32, NULL, NULL);
    return buf[0];
}

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
{
    int a[8] = {core, v, ssa, pitch, vl, vr, 0x00FF, 0x1FC0};
    memcpy(buf, a, sizeof a);
    call(C_VOICE, sizeof a);
}
static void kon(int core, int mask) { buf[0] = core; buf[1] = mask; call(C_KON, 8); }
static void koff(int core, int mask) { buf[0] = core; buf[1] = mask; call(C_KOFF, 8); }
static void setpitch(int core, int v, int p) { buf[0] = core; buf[1] = v; buf[2] = p; call(C_PITCH, 12); }
static void status(int core, int *endx, int *nax)
{
    buf[0] = core;
    SifCallRpc(&cd, C_STATUS, 0, buf, 4, buf, 4 * 32, NULL, NULL);
    *endx = buf[0];
    for (int i = 0; i < 24; i++) nax[i] = buf[1 + i];
}

static unsigned now_ms(void) { return (unsigned)((unsigned long long)clock() * 1000 / CLOCKS_PER_SEC); }


static int sstat(int slot, int *o) { buf[0] = slot; SifCallRpc(&cd, C_SSTAT, 0, buf, 4, buf, 32, NULL, NULL); memcpy(o, buf, 24); return o[0]; }
static void sstart2(int slot, int vl, int vr, int ringl, int ringr, int pitch, int vol, const char *path, int autoplay)
{
    int a[10] = {slot, 1, vl, vr, ringl, ringr, pitch, vol, 0 /*pan*/, autoplay};
    memcpy(buf, a, sizeof a);
    strcpy((char *)buf + 64, path);
    int r = call(C_SSTART, 64 + strlen(path) + 1);
    int ss[6];
    unsigned t0 = now_ms();
    for (int i = 0; i < 400; i++) { if (sstat(slot, ss) == (autoplay ? 2 : 5) || ss[0] == 4) break; usleep(5000); }
    LOG("  slot%d %s %s: %u ms, state=%d err=%d\n", slot, autoplay ? "start" : "prep ", path + 8, now_ms() - t0, ss[0], ss[3]);
}
static void sstart(int slot, int vl, int vr, int ringl, int ringr, int pitch, int vol, const char *path)
{ sstart2(slot, vl, vr, ringl, ringr, pitch, vol, path, 1); }
static void splay(int slot) { buf[0] = slot; call(C_SPLAY, 4); }
static void sstop(int slot) { buf[0] = slot; call(C_SSTOP, 4); usleep(30 * 1000); }
static void svol(int vl, int vr, int vol) { buf[0] = 1; buf[1] = vl; buf[2] = vol; buf[3] = 0; call(C_VOL, 16); buf[0] = 1; buf[1] = vr; buf[2] = 0; buf[3] = vol; call(C_VOL, 16); }
static void spanvol(int slot, int vol, int pan) { buf[0] = slot; buf[1] = vol; buf[2] = pan; call(C_SVOL, 12); }
static void vpan(int v, int vol, int pan, int mono) { buf[0] = 1; buf[1] = v; buf[2] = vol; buf[3] = pan; buf[4] = mono; call(C_VPAN, 20); }
static void sfx(int which, float rate) { voice(1, 4, which, (int)(4096 * 22050 / 48000 * rate), 0x3FFF, 0x3FFF); kon(1, 1 << 4); }

int main_tests(int *spu)
{
    const int base = 4096 * 44100 / 48000;
    int ss[6];
    const char *XP = "cdrom0:\\XPART.VAS;1", *ZC = "cdrom0:\\ZZZC.VAS;1", *MN = "cdrom0:\\MENU.VAS;1";
    (void)spu;
    scr_clear();
    LOG("PHASE 3: short track (8 s) x3 loops: listen for clicks\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, 0x3000, ZC);
    usleep(25 * 1000 * 1000);
    sstat(0, ss); LOG("  halves=%d under=%d err=%d\n", ss[1], ss[2], ss[3]);
    sstop(0);
    usleep(1000 * 1000);

    LOG("PHASE 4a: plain stop+start x3 (XP, MENU, ZC), 4 s each\n");
    const char *seq[] = {XP, MN, ZC};
    for (int k = 0; k < 3; k++) {
        sstart(0, 0, 1, 0x100000, 0x110000, base, 0x3000, seq[k]);
        usleep(4 * 1000 * 1000);
        sstop(0);
    }
    usleep(1000 * 1000);
    LOG("PHASE 4b: prepared switch (next track preloaded on slot1, then play+stop at once)\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, 0x3000, XP);
    usleep(3 * 1000 * 1000);
    sstart2(1, 6, 7, 0x120000, 0x130000, base, 0x3000, MN, 0);
    usleep(500 * 1000);
    splay(1); sstop(0);
    LOG("  switched\n");
    usleep(4 * 1000 * 1000);
    sstart2(0, 0, 1, 0x100000, 0x110000, base, 0x3000, ZC, 0);
    usleep(500 * 1000);
    splay(0); sstop(1);
    LOG("  switched\n");
    usleep(4 * 1000 * 1000);
    sstop(0);
    usleep(1000 * 1000);

    LOG("PHASE 4c: crossfade ZC -> MENU over 2 s\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, 0x3000, ZC);
    usleep(3 * 1000 * 1000);
    sstart(1, 6, 7, 0x120000, 0x130000, base, 0, MN);
    for (int v = 0; v <= 20; v++) { spanvol(0, 0x3000 * (20 - v) / 20, 0); spanvol(1, 0x3000 * v / 20, 0); usleep(100 * 1000); }
    sstop(0);
    usleep(3 * 1000 * 1000);
    sstat(1, ss); LOG("  slot1 halves=%d under=%d err=%d\n", ss[1], ss[2], ss[3]);
    sstop(1);
    return 0;
}


#ifdef PAN
/* panning test with cyber_battle: mono stream, stereo stream, resident SFX stems */
static void sweep(int slot, int vol, int from, int to, int ms)
{
    for (int i = 0; i <= 40; i++) { spanvol(slot, vol, from + (to - from) * i / 40); usleep(ms * 1000 / 40); }
}
int pan_tests(void)
{
    const int base = 4096 * 44100 / 48000, V = 0x3000;
    int ss[6];
    scr_clear();
    LOG("PAN A: cyber_battle_backing (MONO stream, equal-power)\n");
    LOG("  center 3s, L->R 4s, R->L 4s, hard L 2s, hard R 2s\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, V, "cdrom0:\\CBACK.VAS;1");
    usleep(3000 * 1000);
    sweep(0, V, -4096, 4096, 4000); /* note: starts with a jump to hard left */
    sweep(0, V, 4096, -4096, 4000);
    usleep(2000 * 1000);
    sweep(0, V, -4096, 4096, 200);
    usleep(2000 * 1000);
    sstat(0, ss); LOG("  halves=%d under=%d err=%d\n", ss[1], ss[2], ss[3]);
    sstop(0);
    usleep(1000 * 1000);

    LOG("PAN B: cyber_battle_prelude (STEREO stream, balance)\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, V, "cdrom0:\\CPRE.VAS;1");
    usleep(2500 * 1000);
    sweep(0, V, 0, -4096, 1500);
    usleep(1500 * 1000);
    sweep(0, V, -4096, 4096, 3000);
    usleep(1500 * 1000);
    sweep(0, V, 4096, 0, 1500);
    usleep(1500 * 1000);
    sstat(0, ss); LOG("  halves=%d under=%d err=%d\n", ss[1], ss[2], ss[3]);
    sstop(0);
    usleep(1000 * 1000);

    LOG("PAN C: SFX stems: arp=hard L, bongo=center, chord=hard R (together, 8 s)\n");
    {
        const int sp[3] = {0x20000, 0x40000, 0x60000};
        const int pn[3] = {-4096, 0, 4096};
        for (int k = 0; k < 3; k++) {
            voice(1, 2 + k, sp[k], 4096 * 22050 / 48000, 0, 0);
            vpan(2 + k, V, pn[k], 1);
        }
        kon(1, 0x1C);
        usleep(8500 * 1000);
    }
    LOG("PAN D: arp swept L->R->L while bongo sits center (8 s)\n");
    {
        voice(1, 2, 0x20000, 4096 * 22050 / 48000, 0, 0);
        voice(1, 3, 0x40000, 4096 * 22050 / 48000, 0, 0);
        vpan(3, V, 0, 1); vpan(2, V, -4096, 1);
        kon(1, 0x0C);
        for (int i = 0; i <= 80; i++) {
            int t = i < 40 ? i : 80 - i;
            vpan(2, V, -4096 + 8192 * t / 40, 1);
            usleep(100 * 1000);
        }
    }
    usleep(1000 * 1000);
    LOG("PAN E: stream (mono) at pan -2048 + arp SFX at +2048\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, V, "cdrom0:\\CBACK.VAS;1");
    spanvol(0, V, -2048);
    usleep(1500 * 1000);
    voice(1, 2, 0x20000, 4096 * 22050 / 48000, 0, 0); vpan(2, V, 2048, 1); kon(1, 4);
    usleep(5000 * 1000);
    sstop(0);
    LOG("pan test finished\n");
    return 0;
}
#endif

int loop_tests(void)
{
    const int base = 4096 * 44100 / 48000;
    int ss[6];
    scr_clear();
    LOG("LOOP TEST A: mus_prebattle1 (10.5 s) x3 = 32 s, source loop is seamless\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, 0x3000, "cdrom0:\\\\PRE.VAS;1");
    usleep(33 * 1000 * 1000);
    sstat(0, ss); LOG("  halves=%d under=%d err=%d\n", ss[1], ss[2], ss[3]);
    sstop(0);
    usleep(2 * 1000 * 1000);
    LOG("LOOP TEST B: mus_undynepiano (8.6 s) x3 = 26 s\n");
    sstart(0, 0, 1, 0x100000, 0x110000, base, 0x3000, "cdrom0:\\\\PIANO.VAS;1");
    usleep(27 * 1000 * 1000);
    sstat(0, ss); LOG("  halves=%d under=%d err=%d\n", ss[1], ss[2], ss[3]);
    sstop(0);
    return 0;
}

int main(void)
{
    init_scr();
    LOG("engsnd audio test\n");
    SifInitRpc(0);
#ifndef NORESET
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
#endif
    SifInitRpc(0);
    SifLoadFileInit();
    sceCdInit(SCECdINIT);
    for (int i = 0; i < 200 && sceCdDiskReady(0) == SCECdNotReady; i++) usleep(50000);
    sbv_patch_enable_lmb();
    int m1 = 0, m2 = 0;
    int r1 = SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &m1);
    int r2 = SifExecModuleBuffer(engsnd_irx, size_engsnd_irx, 0, NULL, &m2);
    LOG("freesd id=%d res=%d  engsnd id=%d res=%d\n", r1, m1, r2, m2);
    for (int tries = 0; tries < 200; tries++) {
        if (SifBindRpc(&cd, DEV, 0) >= 0 && cd.server) break;
        usleep(20000);
    }
    LOG("bind server=%p\n", cd.server);
    if (!cd.server) { SleepThread(); }
    buf = memalign(64, 0x10000);
    LOG("init=%d\n", call(C_INIT, 4));

    // SPU2 layout (core 0): data from 0x5010. sfx0,1,2, music L, R
    const int *tab = (const int *)testaudio_bin;
    int n = tab[0], spu[8], at = 0x5010;
    for (int i = 0; i < n; i++) {
        int off = tab[1 + 2 * i], len = tab[2 + 2 * i];
        spu[i] = at;
        upload(testaudio_bin + off, len, at);
        at += (len + 15) & ~15;
        LOG("asset %d: spu 0x%06X len %d\n", i, spu[i], len);
    }
#ifdef PAN
    (void)spu;
    { const int *pt = (const int *)panaudio_bin;
      for (int i = 0; i < pt[0]; i++) upload(panaudio_bin + pt[1 + 2 * i], pt[2 + 2 * i], 0x20000 * (i + 1)); }
    pan_tests();
#elif defined(LOOP2)
    (void)spu; loop_tests();
#else
    main_tests(spu);
#endif
    LOG("done.\n");
    SleepThread();
    return 0;
}
