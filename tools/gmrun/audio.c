// audio.c - PS2 backend for the GML audio layer (ge/audio.h). Talks to the engsnd IOP sound server (freesd based):
//  - SFX: mono SPU-ADPCM bodies from snd.bnk are uploaded on demand into SPU RAM (cache, wiped when full) and played on core 1
//    voices 8..23; looping sounds use a copy whose last block carries the loop flag.
//  - music: stereo .vas files streamed from the HDD by the IOP into two rings (2 stream slots, voices 0/1 and 6/7 of core 1).
// All RPCs are blocking but short; music starts are asynchronous (the IOP prepares the stream on its own).
// Gains passed by the core are effective gains (asset volume included).
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "ge/audio.h"
#include "ge/io.h"
#include "ge/log.h"
#include "gml/gm_sounds.h"

extern unsigned char freesd_irx[]; extern unsigned int size_freesd_irx;
extern unsigned char engsnd_x_irx[]; extern unsigned int size_engsnd_x_irx;

#define DEV 0x0E460001
enum { C_INIT = 1, C_UPLOAD, C_VOICE, C_KON, C_KOFF, C_PITCH, C_STATUS, C_VOL, C_SSTART, C_SSTOP, C_SSTAT, C_SPLAY, C_SVOL, C_VPAN };
#define BASE (4096 * 44100 / 48000)
#define SFXBASE (4096 * 22050 / 48000)
#define MUSVOL 0x3000
#define SFXVOL 0x3000
#define SPU_SFX_START 0x5010
#define SPU_SFX_END 0x100000
#define V0 8                      /* first SFX voice */
#define NV 16                     /* SFX voices 8..23 */
#define NB 40

static SifRpcClientData_t cd;
static int *buf;
static ge_file_t *bank;

static int call(int cmd, int n) { SifCallRpc(&cd, cmd, 0, buf, n, buf, 4 * 32, NULL, NULL); return buf[0]; }
static void rvoice(int v, int ssa, int pitch, int vl, int vr)
{ int a[8] = {1, v, ssa, pitch, vl, vr, 0x00FF, 0x1FC0}; memcpy(buf, a, sizeof a); call(C_VOICE, sizeof a); }
static void rkon(int mask) { buf[0] = 1; buf[1] = mask; call(C_KON, 8); }
static void rkoff(int mask) { buf[0] = 1; buf[1] = mask; call(C_KOFF, 8); }
static void rpitch(int v, int p) { buf[0] = 1; buf[1] = v; buf[2] = p; call(C_PITCH, 12); }
static void rvol(int v, int vl, int vr) { buf[0] = 1; buf[1] = v; buf[2] = vl; buf[3] = vr; call(C_VOL, 16); }
static int rsstat(int slot) { buf[0] = slot; SifCallRpc(&cd, C_SSTAT, 0, buf, 4, buf, 64, NULL, NULL); return buf[0]; }

/* ---- backend handles ---- */
typedef struct {
    int used, serial, music, idx;      /* idx: SFX voice number or stream slot (-1 = music waiting for a free slot) */
    int snd, loop, paused, applied;
    unsigned t0;
    float gain, pitch;
} bh_t;
static bh_t B[NB];
static int serial = 1;
static int slot_owner[2] = {-1, -1}, stop_req[2];
static unsigned last_chk;

static int h_of(int i) { return (B[i].serial << 6) | i; }
static bh_t *get(int h) { if (h < 0) return NULL; bh_t *b = &B[h & 63]; return b->used && b->serial == (h >> 6) ? b : NULL; }

/* ---- SPU RAM cache for SFX ---- */
typedef struct { int snd, loop, spu; } ce_t;
static ce_t cache[640];
static int ncache, spu_at = SPU_SFX_START;
static void spu_wipe(void)
{
    rkoff(0xFFFFFF & ~0xC3);       /* every SFX voice (not the stream voices 0,1,6,7) */
    for (int i = 0; i < NB; i++) if (B[i].used && !B[i].music) B[i].used = 0;
    ncache = 0; spu_at = SPU_SFX_START;
    ge_log("audio: SPU sfx cache wiped");
}
static int sfx_load(int snd, int loop)
{
    for (int i = 0; i < ncache; i++) if (cache[i].snd == snd && cache[i].loop == loop) return cache[i].spu;
    const gm_sounddef *d = &gm_sounds[snd];
    int len = (d->len + 15) & ~15;
    if (!bank || len <= 0) return -1;
    if (spu_at + len > SPU_SFX_END || ncache == 640) spu_wipe();
    unsigned char *tmp = ge_io_alloc(len);
    if (!tmp) return -1;
    memset(tmp, 0, len);
    if (ge_io_pread(bank, d->off, tmp, d->len) != d->len) { ge_log("audio: bank read failed for %s", d->name); ge_io_free(tmp); return -1; }
    if (loop) tmp[len - 16 + 1] = 3;               /* last block: loop back to the start */
    for (int o = 0; o < len; o += 0xE000) {
        int n = len - o > 0xE000 ? 0xE000 : len - o;
        buf[0] = spu_at + o; buf[1] = n;
        memcpy((unsigned char *)buf + 64, tmp + o, n);
        SifCallRpc(&cd, C_UPLOAD, 0, buf, 64 + n, buf, 16, NULL, NULL);
    }
    ge_io_free(tmp);
    int at = spu_at;
    cache[ncache++] = (ce_t){snd, loop, at};
    spu_at += len;
    return at;
}
static int svol(float g) { int v = (int)(SFXVOL * g); return v < 0 ? 0 : v > 0x3FFF ? 0x3FFF : v; }

/* ---- music ---- */
static void stream_begin(bh_t *b, int slot)
{
    char path[96];
    snprintf(path, sizeof path, "pfs0:/current/%s.vas", gm_sounds[b->snd].name);
    int vl = slot ? 6 : 0, vr = slot ? 7 : 1, ringl = slot ? 0x120000 : 0x100000, ringr = ringl + 0x10000;
    int vol = (int)(MUSVOL * b->gain);
    int a[10] = {slot, 1, vl, vr, ringl, ringr, (int)(BASE * b->pitch), vol, 0, 1};
    memcpy(buf, a, sizeof a);
    strcpy((char *)buf + 64, path);
    int r = call(C_SSTART, 64 + strlen(path) + 1);
    b->idx = slot; b->t0 = ge_time_ms();
    slot_owner[slot] = (int)(b - B);
    ge_log("audio: music %s on slot %d (%d)", gm_sounds[b->snd].name, slot, r);
}
static void stream_kill(int slot)
{
    buf[0] = slot; call(C_SSTOP, 4);
    stop_req[slot] = 1; slot_owner[slot] = -1;
}
static int slot_idle(int slot) { int s = rsstat(slot); return s == 0 || s == 4; }

/* ---- backend interface ---- */
static int be_start(int snd, int loop, float gain, float pitch)
{
    if (snd < 0 || snd >= gm_nsounds) return -1;
    const gm_sounddef *d = &gm_sounds[snd];
    if (d->kind == 0) {
        int spu = sfx_load(snd, loop);
        if (spu < 0) return -1;
        int v = -1;                                   /* free voice, else the oldest SFX */
        for (int x = 0; x < NV && v < 0; x++) {
            int used = 0;
            for (int i = 0; i < NB; i++) if (B[i].used && !B[i].music && B[i].idx == V0 + x) used = 1;
            if (!used) v = V0 + x;
        }
        int bi = -1;
        for (int i = 0; i < NB; i++) if (!B[i].used) { bi = i; break; }
        if (v < 0 || bi < 0) {
            int o = -1;
            for (int i = 0; i < NB; i++) if (B[i].used && !B[i].music && (o < 0 || B[i].t0 < B[o].t0)) o = i;
            if (o < 0) return -1;
            if (v < 0) v = B[o].idx;
            B[o].used = 0;
            if (bi < 0) bi = o;
        }
        B[bi] = (bh_t){1, serial++ & 0x3FF, 0, v, snd, loop, 0, 0, ge_time_ms(), gain, pitch};
        rkoff(1 << v);
        rvoice(v, spu, (int)(SFXBASE * pitch), svol(gain), svol(gain));
        rkon(1 << v);
        return h_of(bi);
    }
    if (d->kind == 1) {
        int bi = -1;
        for (int i = 0; i < NB; i++) if (!B[i].used) { bi = i; break; }
        if (bi < 0) return -1;
        bh_t *b = &B[bi];
        *b = (bh_t){1, serial++ & 0x3FF, 1, -1, snd, loop, 0, 0, ge_time_ms(), gain, pitch};
        /* the start is deferred to be_tick: the game sets pitch/gain right after playing, and the IOP would overwrite changes made while it prepares the stream */
        if (slot_owner[0] >= 0 && slot_owner[1] >= 0) {   /* both busy: drop the oldest stream */
            int o = B[slot_owner[0]].t0 <= B[slot_owner[1]].t0 ? 0 : 1;
            B[slot_owner[o]].used = 0; stream_kill(o);
        }
        return h_of(bi);
    }
    return -1;
}
static void be_set(int h, float gain, float pitch)
{
    bh_t *b = get(h); if (!b) return;
    int changed_p = pitch != b->pitch;
    b->gain = gain; b->pitch = pitch;
    if (!b->music) {
        rvol(b->idx, svol(gain), svol(gain));
        if (changed_p) rpitch(b->idx, (int)(SFXBASE * pitch));
    } else if (b->idx >= 0) {
        b->applied = 0;                               /* re-sent by be_tick once the stream is really playing */
        buf[0] = b->idx; buf[1] = b->paused ? 0 : (int)(MUSVOL * gain); buf[2] = 0; call(C_SVOL, 12);
        if (changed_p) { int vl = b->idx ? 6 : 0; rpitch(vl, (int)(BASE * pitch)); rpitch(vl + 1, (int)(BASE * pitch)); }
    }
}
static void be_pause(int h, int on)
{
    bh_t *b = get(h); if (!b) return;
    b->paused = on;
    if (!b->music) {
        if (on) rvol(b->idx, 0, 0); else rvol(b->idx, svol(b->gain), svol(b->gain));
    } else if (b->idx >= 0) {
        buf[0] = b->idx; buf[1] = on ? 0 : (int)(MUSVOL * b->gain); buf[2] = 0; call(C_SVOL, 12);
    }
}
static void be_stop(int h)
{
    bh_t *b = get(h); if (!b) return;
    if (!b->music) rkoff(1 << b->idx);
    else if (b->idx >= 0 && slot_owner[b->idx] == (int)(b - B)) stream_kill(b->idx);
    b->used = 0;
}
static int be_playing(int h) { return get(h) != NULL; }

static void be_tick(void)
{
    unsigned now = ge_time_ms();
    /* streams: finish pending stops, start waiting music, drop errored / ended streams */
    for (int s = 0; s < 2; s++) {
        if (stop_req[s]) {
            int st = rsstat(s);
            if (st == 0 || st == 4) stop_req[s] = 0;
            else if (st == 2 || st == 5) { buf[0] = s; call(C_SSTOP, 4); }
        }
    }
    int chk = now - last_chk >= 300;
    for (int i = 0; i < NB; i++) {
        bh_t *b = &B[i];
        if (!b->used || !b->music) continue;
        if (b->idx < 0) {                             /* waiting for a slot */
            for (int s = 0; s < 2; s++) if (slot_owner[s] < 0 && !stop_req[s] && slot_idle(s)) { stream_begin(b, s); break; }
            continue;
        }
        if (!b->loop && now - b->t0 >= (unsigned)(gm_sounds[b->snd].dur_ms / b->pitch)) {
            ge_log("audio: music %s ended", gm_sounds[b->snd].name);
            stream_kill(b->idx); b->used = 0; continue;
        }
        if (!b->applied) {
            int st = rsstat(b->idx);
            if (st == 2) {
                int vl = b->idx ? 6 : 0;
                buf[0] = b->idx; buf[1] = b->paused ? 0 : (int)(MUSVOL * b->gain); buf[2] = 0; call(C_SVOL, 12);
                rpitch(vl, (int)(BASE * b->pitch)); rpitch(vl + 1, (int)(BASE * b->pitch));
                b->applied = 1;
            }
        }
        if (chk && now - b->t0 > 800) {
            int st = rsstat(b->idx);
            if (st == 4 || st == 0) { ge_log("audio: stream %s gone (state %d)", gm_sounds[b->snd].name, st); slot_owner[b->idx] = -1; b->used = 0; }
        }
    }
    if (chk) last_chk = now;
    /* SFX: ENDX tells which one-shot voices ran out */
    int any = 0;
    for (int i = 0; i < NB; i++) if (B[i].used && !B[i].music && !B[i].loop) any = 1;
    if (any) {
        buf[0] = 1; SifCallRpc(&cd, C_STATUS, 0, buf, 4, buf, 4 * 32, NULL, NULL);
        unsigned endx = buf[0];
        for (int i = 0; i < NB; i++) {
            bh_t *b = &B[i];
            if (!b->used || b->music || b->loop) continue;
            unsigned age = now - b->t0;
            if ((age > 50 && (endx >> b->idx & 1)) || age > (unsigned)(gm_sounds[b->snd].dur_ms / b->pitch) + 300) { rkoff(1 << b->idx); b->used = 0; }
        }
    }
}

static const ge_audio_be be = { be_start, be_set, be_pause, be_stop, be_playing, be_tick };

int gmrun_audio_init(void)
{
    sbv_patch_enable_lmb();
    int m1 = 0, m2 = 0;
    int r1 = SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &m1);
    int r2 = SifExecModuleBuffer(engsnd_x_irx, size_engsnd_x_irx, 0, NULL, &m2);
    ge_log("audio: freesd id=%d res=%d engsnd id=%d res=%d", r1, m1, r2, m2);
    for (int t = 0; t < 200; t++) { if (SifBindRpc(&cd, DEV, 0) >= 0 && cd.server) break; usleep(20000); }
    if (!cd.server) { ge_log("audio: no engsnd server, running silent"); return -1; }
    buf = memalign(64, 0x10000 + 64);
    int r = call(C_INIT, 4);
    bank = ge_io_open("snd.bnk");
    ge_log("audio: init %d, bank %s (%d bytes)", r, bank ? "ok" : "MISSING", bank ? ge_io_size(bank) : 0);
    ge_audio = &be;
    return 0;
}
