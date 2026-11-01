/* engsnd.irx - PS2GameEngine sound server (IOP). RPC layer over libsd/freesd:
 *  - voice control (pitch/volume/ADSR), SPU2 RAM upload, key on/off, position/end status (SFX, resident samples)
 *  - music streaming: the IOP reads a stereo/mono ADPCM file and keeps a 2-half ring buffer per channel filled in SPU2 RAM. */
#include "irx_imports.h"

IRX_ID("engsnd", 1, 0);

#define ENGSND_DEV 0x0E460001
#define BUFSZ      (64 * 1024)

enum { C_INIT = 1, C_UPLOAD, C_VOICE, C_KON, C_KOFF, C_PITCH, C_STATUS, C_VOL, C_SSTART, C_SSTOP, C_SSTAT, C_SPLAY, C_SVOL, C_VPAN };

#define MAXSTREAMS 2
#define HALF       0x8000 /* bytes per ring half per channel */
#define CHUNK      2048
#define FBUF       ((HALF / CHUNK + 2) * 2 * CHUNK)
#define SPU_DMA_CH 1

enum { ST_IDLE = 0, ST_START, ST_PLAY, ST_STOP, ST_ERR, ST_READY, ST_GO };
typedef struct {
    volatile int state;
    char path[128];
    int core, vL, vR, ringL, ringR, pitch, vol, pan, tl, tr, cl, cr, autoplay;
    int fd, ch, loop_bytes, pos, last_half, halves, under, err, mask;
    int fmax_us, fslow, fshort, wraps, fwrap_us;   /* diagnostics: slowest refill (read+DMA), refills > 100 ms, short file reads, loop wraps, refill time that included the wrap */
} stream_t;

static stream_t st[MAXSTREAMS];
static unsigned char fbuf[FBUF] __attribute__((aligned(64)));
static unsigned char chb[2][HALF] __attribute__((aligned(64)));
static unsigned char rpcbuf[BUFSZ] __attribute__((aligned(64)));
static SifRpcDataQueue_t qd;
static SifRpcServerData_t sd;

static int do_init(void)
{
    int c, r = sceSdInit(0);
    for (c = 0; c < 2; c++) {
        sceSdSetParam(c | SD_PARAM_MMIX, 0xFF0);
        sceSdSetParam(c | SD_PARAM_EVOLL, 0);
        sceSdSetParam(c | SD_PARAM_EVOLR, 0);
        sceSdSetSwitch(c | SD_SWITCH_VMIXL, 0xFFFFFF);
        sceSdSetSwitch(c | SD_SWITCH_VMIXR, 0xFFFFFF);
        sceSdSetSwitch(c | SD_SWITCH_VMIXEL, 0);
        sceSdSetSwitch(c | SD_SWITCH_VMIXER, 0);
        sceSdSetSwitch(c | SD_SWITCH_KOFF, 0xFFFFFF);
    }
    /* same routing as audsrv: core 1 is the output core, core 0 is muted */
    sceSdSetParam(1 | SD_PARAM_AVOLL, 0x7FFF);
    sceSdSetParam(1 | SD_PARAM_AVOLR, 0x7FFF);
    sceSdSetParam(0 | SD_PARAM_BVOLL, 0);
    sceSdSetParam(0 | SD_PARAM_BVOLR, 0);
    sceSdSetParam(1 | SD_PARAM_BVOLL, 0x7FFF);
    sceSdSetParam(1 | SD_PARAM_BVOLR, 0x7FFF);
    sceSdSetParam(0 | SD_PARAM_MVOLL, 0);
    sceSdSetParam(0 | SD_PARAM_MVOLR, 0);
    sceSdSetParam(1 | SD_PARAM_MVOLL, 0x3FFF);
    sceSdSetParam(1 | SD_PARAM_MVOLR, 0x3FFF);
    return r;
}

static void spu_write(int ch, const void *iop, int spu, int n)
{
    sceSdVoiceTrans(ch, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, (u8 *)iop, (u32 *)spu, n);
    sceSdVoiceTransStatus(ch, 1);
}

/* ---------------- streaming ---------------- */
static int be32(const unsigned char *p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

/* read n bytes (n<=HALF, multiple of 16) of channel data starting at channel offset p (p+n <= loop_bytes) into chb[0]/chb[1] at dst */
/* disc reads in small slices with a sleep between them, so the IOP stays responsive to RPC while a stream refills */
#define SLICE 8192
static int sliced_read(int fd, void *dst, int n)
{
    int done = 0;
    while (done < n) {
        int m = n - done > SLICE ? SLICE : n - done;
        int r = read(fd, (char *)dst + done, m);
        if (r <= 0) return done ? done : r;
        done += r;
        if (r < m) break;
        DelayThread(1000);
    }
    return done;
}

static unsigned now_us(void) { iop_sys_clock_t c; u32 sec, usec; GetSystemTime(&c); SysClock2USec(&c, &sec, &usec); return sec * 1000000u + usec; }

static int read_segment(stream_t *s, int p, int n, int dst)
{
    if (s->ch == 1) {
        lseek(s->fd, 48 + p, 0);
        { int r = read(s->fd, chb[0] + dst, n); if (r != n) s->fshort++; return r == n ? 0 : -1; }
    }
    int c0 = p / CHUNK, c1 = (p + n - 1) / CHUNK;
    int bytes = (c1 - c0 + 1) * 2 * CHUNK;
    lseek(s->fd, 48 + c0 * 2 * CHUNK, 0);
    int r = read(s->fd, fbuf, bytes);
    if (r < 0) return -1;
    if (r < bytes) { s->fshort++; memset(fbuf + r, 0, bytes - r); }
    for (int done = 0; done < n;) {
        int o = p + done;
        int c = o / CHUNK - c0, in = o % CHUNK;
        int m = CHUNK - in;
        if (m > n - done) m = n - done;
        memcpy(chb[0] + dst + done, fbuf + c * 2 * CHUNK + in, m);
        memcpy(chb[1] + dst + done, fbuf + c * 2 * CHUNK + CHUNK + in, m);
        done += m;
    }
    return 0;
}

/* fill chb[*][0..HALF) with the next HALF bytes of each channel (wrapping at loop_bytes), patch flags, advance pos */
static int fill_half(stream_t *s)
{
    int got = 0, wrapped = 0;
    while (got < HALF) {
        int n = HALF - got;
        if (n > s->loop_bytes - s->pos) n = s->loop_bytes - s->pos;
        if (read_segment(s, s->pos, n, got) < 0) return -1;
        got += n;
        s->pos += n;
        if (s->pos >= s->loop_bytes) { s->pos = 0; s->wraps++; wrapped = 1; }
    }
    for (int c = 0; c < s->ch; c++)
        for (int o = 1; o < HALF; o += 16) chb[c][o] = 0; /* ring flags: all plain, ring first/last patched by caller */
    s->fwrap_us = wrapped;
    return 0;
}

static void write_half(stream_t *s, int half, int first_of_ring, int last_of_ring)
{
    for (int c = 0; c < s->ch; c++) {
        int ring = c ? s->ringR : s->ringL;
        if (first_of_ring) chb[c][1] = 0x04;
        if (last_of_ring) chb[c][HALF - 16 + 1] = 0x03;
        spu_write(SPU_DMA_CH, chb[c], ring + half * HALF, HALF);
    }
}


/* ---------------- panning ----------------
 * pan: -4096 (hard left) .. 0 (center) .. +4096 (hard right); vol: 0..0x3FFF.
 * mono source : equal-power law, gains clamped so both sides are 1.0 at the center and the near side is 1.0 at the extremes
 *               (a centered sound is as loud as an unpanned one; the far side falls off as sin/cos).
 * stereo music: balance (the near channel stays at 1.0, the far channel is attenuated linearly). */
#define PAN_MAX 4096
static const short costab[65] = {16384,16379,16364,16340,16305,16261,16207,16143,16069,15986,15893,15791,15679,15557,15426,15286,15137,14978,14811,14635,14449,14256,14053,13842,13623,13395,13160,12916,12665,12406,12140,11866,11585,11297,11003,10702,10394,10080,9760,9434,9102,8765,8423,8076,7723,7366,7005,6639,6270,5897,5520,5139,4756,4370,3981,3590,3196,2801,2404,2006,1606,1205,804,402,0}; /* Q14 cos(i*pi/128) */
static void pan_gains(int mono, int pan, int vol, int *gl, int *gr)
{
    if (pan < -PAN_MAX) pan = -PAN_MAX;
    if (pan > PAN_MAX) pan = PAN_MAX;
    int l, r;
    if (mono) {
        int idx = (pan + PAN_MAX) * 64 / (2 * PAN_MAX);
        l = costab[idx] * 11585 >> 13; /* *sqrt2 */
        r = costab[64 - idx] * 11585 >> 13;
        if (l > 16384) l = 16384;
        if (r > 16384) r = 16384;
    } else {
        l = pan <= 0 ? 16384 : 16384 - pan * 4;
        r = pan >= 0 ? 16384 : 16384 + pan * 4;
    }
    *gl = (vol * l) >> 14;
    *gr = (vol * r) >> 14;
}

static void stream_setvol(stream_t *s, int vol, int pan, int immediate)
{
    s->vol = vol; s->pan = pan;
    pan_gains(s->ch == 1, pan, vol, &s->tl, &s->tr);
    if (immediate) { s->cl = s->tl; s->cr = s->tr; }
}

/* write current volumes to the hardware: ch==2 -> L voice left-only, R voice right-only; ch==1 -> one voice, both sides */
static void stream_applyvol(stream_t *s)
{
    int c = s->core;
    if (s->ch == 2) {
        sceSdSetParam(c | (s->vL << 1) | SD_VPARAM_VOLL, s->cl);
        sceSdSetParam(c | (s->vL << 1) | SD_VPARAM_VOLR, 0);
        sceSdSetParam(c | (s->vR << 1) | SD_VPARAM_VOLL, 0);
        sceSdSetParam(c | (s->vR << 1) | SD_VPARAM_VOLR, s->cr);
    } else {
        sceSdSetParam(c | (s->vL << 1) | SD_VPARAM_VOLL, s->cl);
        sceSdSetParam(c | (s->vL << 1) | SD_VPARAM_VOLR, s->cr);
    }
}

/* slew the hardware volume toward the target (anti-zipper): max 0x400 per 2 ms tick */
static void stream_slew(stream_t *s)
{
    if (s->cl == s->tl && s->cr == s->tr) return;
    int d;
    d = s->tl - s->cl; if (d > 0x400) d = 0x400; if (d < -0x400) d = -0x400; s->cl += d;
    d = s->tr - s->cr; if (d > 0x400) d = 0x400; if (d < -0x400) d = -0x400; s->cr += d;
    stream_applyvol(s);
}

static void stream_begin(stream_t *s)
{
    unsigned char h[48];
    s->err = 0;
    s->fd = open(s->path, 1 /*O_RDONLY*/);
    if (s->fd < 0) { s->err = s->fd; s->state = ST_ERR; return; }
    read(s->fd, h, 48);
    s->ch = h[0x1E] ? h[0x1E] : 1;
    s->loop_bytes = be32(h + 0x14) * 16;
    if (s->loop_bytes < 2 * HALF) { s->err = -100; s->state = ST_ERR; close(s->fd); return; }
    s->pos = 0; s->halves = 0; s->under = 0; s->last_half = 0; s->fmax_us = s->fslow = s->fshort = s->wraps = s->fwrap_us = 0;
    if (fill_half(s) < 0) { s->err = -101; s->state = ST_ERR; return; }
    write_half(s, 0, 1, 0);
    if (fill_half(s) < 0) { s->err = -101; s->state = ST_ERR; return; }
    write_half(s, 1, 0, 1);
    int c = s->core;
    int mask = 1 << s->vL;
    sceSdSetAddr(c | (s->vL << 1) | SD_VADDR_SSA, s->ringL);
    sceSdSetParam(c | (s->vL << 1) | SD_VPARAM_PITCH, s->pitch);
    sceSdSetParam(c | (s->vL << 1) | SD_VPARAM_ADSR1, 0x00FF);
    sceSdSetParam(c | (s->vL << 1) | SD_VPARAM_ADSR2, 0x1FC0);
    if (s->ch == 2) {
        sceSdSetAddr(c | (s->vR << 1) | SD_VADDR_SSA, s->ringR);
        sceSdSetParam(c | (s->vR << 1) | SD_VPARAM_PITCH, s->pitch);
        sceSdSetParam(c | (s->vR << 1) | SD_VPARAM_ADSR1, 0x00FF);
        sceSdSetParam(c | (s->vR << 1) | SD_VPARAM_ADSR2, 0x1FC0);
        mask |= 1 << s->vR;
    }
    stream_setvol(s, s->vol, s->pan, 1);
    stream_applyvol(s);
    s->mask = mask;
    if (s->autoplay) {
        sceSdSetSwitch(c | SD_SWITCH_KON, mask); /* both voices keyed together = phase locked */
        s->state = ST_PLAY;
    } else {
        s->state = ST_READY; /* rings filled, voices programmed; C_SPLAY keys them on instantly */
    }
}

static void stream_update(stream_t *s)
{
    int nax = sceSdGetAddr(s->core | (s->vL << 1) | SD_VADDR_NAX);
    int half = (nax - s->ringL) >= HALF ? 1 : 0;
    if (nax < s->ringL || nax >= s->ringL + 2 * HALF) return;
    if (half == s->last_half) return;
    int refill = s->last_half; /* the half the voice just finished */
    s->last_half = half;
    s->halves++;
    unsigned t0 = now_us();
    if (fill_half(s) < 0) { s->err = -102; s->state = ST_ERR; return; }
    int wr = s->fwrap_us;
    write_half(s, refill, refill == 0, refill == 1);
    unsigned dt = now_us() - t0;
    if ((int)dt > s->fmax_us) s->fmax_us = dt;
    if (dt > 100000) s->fslow++;
    (void)wr;
    /* underrun check: voice already wrapped back into the half we just rewrote */
    nax = sceSdGetAddr(s->core | (s->vL << 1) | SD_VADDR_NAX);
    if ((((nax - s->ringL) >= HALF) ? 1 : 0) == refill) s->under++;
}

static void stream_end(stream_t *s)
{
    sceSdSetSwitch(s->core | SD_SWITCH_KOFF, (1 << s->vL) | (s->ch == 2 ? 1 << s->vR : 0));
    if (s->fd >= 0) close(s->fd);
    s->fd = -1;
    s->state = ST_IDLE;
}

static void stream_thread(void *arg)
{
    (void)arg;
    for (;;) {
        for (int i = 0; i < MAXSTREAMS; i++) {
            stream_t *s = &st[i];
            switch (s->state) {
            case ST_START: stream_begin(s); break;
            case ST_PLAY: stream_update(s); stream_slew(s); break;
            case ST_GO: sceSdSetSwitch(s->core | SD_SWITCH_KON, s->mask); s->state = ST_PLAY; break;
            case ST_STOP: stream_end(s); break;
            default: break;
            }
        }
        DelayThread(2000);
    }
}

/* ---------------- RPC ---------------- */
static void *rpc_func(int cmd, void *data, int size)
{
    int *w = (int *)data;
    int i;
    switch (cmd) {
    case C_INIT:
        w[0] = do_init();
        break;
    case C_UPLOAD: /* w[0]=spu byte addr, w[1]=bytes, payload at +64 */
        spu_write(0, (u8 *)data + 64, w[0], w[1]);
        w[0] = 0;
        break;
    case C_VOICE: { /* core, voice, ssa, pitch, volL, volR, adsr1, adsr2 */
        int e = w[0] | (w[1] << 1);
        sceSdSetAddr(e | SD_VADDR_SSA, w[2]);
        sceSdSetParam(e | SD_VPARAM_PITCH, w[3]);
        sceSdSetParam(e | SD_VPARAM_VOLL, w[4]);
        sceSdSetParam(e | SD_VPARAM_VOLR, w[5]);
        sceSdSetParam(e | SD_VPARAM_ADSR1, w[6]);
        sceSdSetParam(e | SD_VPARAM_ADSR2, w[7]);
        w[0] = 0;
        break;
    }
    case C_VOL: /* core, voice, volL, volR */
        sceSdSetParam((w[0] | (w[1] << 1)) | SD_VPARAM_VOLL, w[2]);
        sceSdSetParam((w[0] | (w[1] << 1)) | SD_VPARAM_VOLR, w[3]);
        w[0] = 0;
        break;
    case C_PITCH: /* core, voice, pitch */
        sceSdSetParam((w[0] | (w[1] << 1)) | SD_VPARAM_PITCH, w[2]);
        w[0] = 0;
        break;
    case C_KON:
        sceSdSetSwitch(w[0] | SD_SWITCH_KON, w[1]);
        w[0] = 0;
        break;
    case C_KOFF:
        sceSdSetSwitch(w[0] | SD_SWITCH_KOFF, w[1]);
        w[0] = 0;
        break;
    case C_STATUS: { /* core -> w[0]=ENDX, w[1..24]=NAX voice 0..23 */
        int c = w[0];
        w[0] = sceSdGetSwitch(c | SD_SWITCH_ENDX);
        for (i = 0; i < 24; i++)
            w[1 + i] = sceSdGetAddr(c | (i << 1) | SD_VADDR_NAX);
        break;
    }
    case C_SSTART: { /* w[0]=slot, [1]=core,[2]=vL,[3]=vR,[4]=ringL,[5]=ringR,[6]=pitch,[7]=volL,[8]=volR, path at +64 */
        stream_t *s = &st[w[0] & (MAXSTREAMS - 1)];
        if (s->state != ST_IDLE && s->state != ST_ERR) { w[0] = -1; break; }
        s->core = w[1]; s->vL = w[2]; s->vR = w[3]; s->ringL = w[4]; s->ringR = w[5];
        s->pitch = w[6]; s->vol = w[7]; s->pan = w[8]; s->autoplay = w[9];
        for (i = 0; i < 127 && ((char *)data)[64 + i]; i++) s->path[i] = ((char *)data)[64 + i];
        s->path[i] = 0;
        s->state = ST_START;
        w[0] = 0;
        break;
    }
    case C_SVOL: { /* slot, vol, pan: smooth (slewed) volume/pan change of a running or prepared stream */
        stream_t *s = &st[w[0] & (MAXSTREAMS - 1)];
        int imm = (s->state == ST_READY);
        if (s->ch == 0) { s->vol = w[1]; s->pan = w[2]; } else stream_setvol(s, w[1], w[2], imm);
        if (imm) stream_applyvol(s);
        w[0] = 0;
        break;
    }
    case C_VPAN: { /* core, voice, vol, pan, mono(1=mono source): direct voice volume with pan law (for resident SFX voices) */
        int l, r;
        pan_gains(w[4], w[3], w[2], &l, &r);
        sceSdSetParam((w[0] | (w[1] << 1)) | SD_VPARAM_VOLL, l);
        sceSdSetParam((w[0] | (w[1] << 1)) | SD_VPARAM_VOLR, r);
        w[0] = 0;
        break;
    }
    case C_SPLAY:
        if (st[w[0] & (MAXSTREAMS - 1)].state == ST_READY) st[w[0] & (MAXSTREAMS - 1)].state = ST_GO;
        w[0] = 0;
        break;
    case C_SSTOP:
        if (st[w[0] & (MAXSTREAMS - 1)].state == ST_PLAY || st[w[0] & (MAXSTREAMS - 1)].state == ST_READY) st[w[0] & (MAXSTREAMS - 1)].state = ST_STOP;
        w[0] = 0;
        break;
    case C_SSTAT: { /* slot -> state, halves, under, err, pos(blocks in channel stream), fd */
        stream_t *s = &st[w[0] & (MAXSTREAMS - 1)];
        w[0] = s->state; w[1] = s->halves; w[2] = s->under; w[3] = s->err; w[4] = s->pos / 16; w[5] = s->fd;
        w[7] = s->fmax_us; w[8] = s->fslow; w[9] = s->fshort; w[10] = s->wraps;
        { /* w[6] = ADPCM blocks (per channel) the voice has played so far, from the live NAX (exact playhead) */
            int nax = sceSdGetAddr(s->core | (s->vL << 1) | SD_VADDR_NAX), off = nax - s->ringL, h = s->halves;
            if (off < 0 || off >= 2 * HALF) off = 0;
            if (((off >= HALF) ? 1 : 0) != (h & 1)) h++; /* half switch not yet counted by the stream thread */
            w[6] = h * (HALF / 16) + (off % HALF) / 16;
        }
        break;
    }
    default:
        w[0] = -1;
    }
    (void)size;
    return data;
}

static void server_thread(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&qd, GetThreadId());
    sceSifRegisterRpc(&sd, ENGSND_DEV, rpc_func, rpcbuf, NULL, NULL, &qd);
    sceSifRpcLoop(&qd);
}

int _start(int argc, char **argv)
{
    iop_thread_t t;
    int id, i;
    (void)argc; (void)argv;
    for (i = 0; i < MAXSTREAMS; i++) { st[i].state = ST_IDLE; st[i].fd = -1; }
    t.attr = TH_C; t.thread = server_thread; t.priority = 30; t.stacksize = 0x4000; t.option = 0;
    id = CreateThread(&t);
    if (id <= 0) return 1;
    StartThread(id, 0);
    t.thread = stream_thread; t.priority = 36; t.stacksize = 0x4000;
    id = CreateThread(&t);
    if (id <= 0) return 1;
    StartThread(id, 0);
    return 0;
}
