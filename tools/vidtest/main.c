// vidplay: PS2GameEngine video player test. MPEG-2 ES decoded by the IPU (patched ps2sdk libmpeg), streamed from disc through a
// ring buffer, displayed on the GS; audio is an engsnd SPU2 stream and is the master clock (frames are dropped/held to follow it).
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <time.h>
#include <dma.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>
#include <gs_gp.h>
#include <draw.h>
#include <graph.h>
#include <packet.h>
#include <libmpeg.h>
#include <debug.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libcdvd.h>
#include <sbv_patches.h>

extern unsigned char freesd_irx[]; extern unsigned int size_freesd_irx;
extern unsigned char engsnd_irx[]; extern unsigned int size_engsnd_irx;

#define SW 640
#define SH 480
#define DEV 0x0E460001
enum { C_INIT = 1, C_UPLOAD, C_VOICE, C_KON, C_KOFF, C_PITCH, C_STATUS, C_VOL, C_SSTART, C_SSTOP, C_SSTAT, C_SPLAY, C_SVOL, C_VPAN };

static unsigned ms(void) { return (unsigned)((unsigned long long)clock() * 1000 / CLOCKS_PER_SEC); }

/* ---------------- engsnd RPC ---------------- */
static SifRpcClientData_t cd; static int *buf;
static int call(int cmd, int sendbytes) { SifCallRpc(&cd, cmd, 0, buf, sendbytes, buf, 4 * 32, NULL, NULL); return buf[0]; }
static int sstat(int slot, int *o) { buf[0] = slot; SifCallRpc(&cd, C_SSTAT, 0, buf, 4, buf, 32, NULL, NULL); memcpy(o, buf, 28); return o[0]; }
static void sstart(int slot, int pitch, int vol, const char *path, int autoplay)
{
    int a[10] = {slot, 1, 0, 1, 0x100000, 0x110000, pitch, vol, 0, autoplay};
    memcpy(buf, a, sizeof a); strcpy((char *)buf + 64, path); call(C_SSTART, 64 + strlen(path) + 1);
}
static void splay(int slot) { buf[0] = slot; call(C_SPLAY, 4); }
static void sstop(int slot) { buf[0] = slot; call(C_SSTOP, 4); usleep(30 * 1000); }

/* ---------------- video file ring ---------------- */
#define RING (1 << 20)
#define RCHUNK (32 * 1024)
static unsigned char *ring;
static volatile unsigned rhead, rtail; /* monotonically increasing byte counters */
static volatile int reof, rquit; static int rfd;
static volatile unsigned ring_empty, rd_n, rd_slow, rd_max_us, rd_total_us, rd_ring_min = 0xffffffffu;
static unsigned us(void) { return (unsigned)((unsigned long long)clock() * 1000000ull / CLOCKS_PER_SEC); }
static char rstack[16384] __attribute__((aligned(16)));
static void reader(void *a)
{
    (void)a;
    while (!rquit && !reof) {
        if (rhead - rtail > RING - 2 * RCHUNK) { usleep(2000); continue; }
        unsigned pos = rhead % RING;
        unsigned ra = us();
        int n = read(rfd, ring + pos, RCHUNK);
        { unsigned d = us() - ra; rd_n++; rd_total_us += d; if (d > rd_max_us) rd_max_us = d; if (d > 30000) rd_slow++; }
        { unsigned fill = rhead - rtail; if (fill < rd_ring_min) rd_ring_min = fill; }
        if (n <= 0) { reof = 1; break; }
        if (n & 15) { int pad = 16 - (n & 15); memset(ring + pos + n, 0, pad); n += pad; reof = 1; }
        SyncDCache(ring + pos, ring + pos + n);
        rhead += n;
    }
    ExitDeleteThread();
}
static int data_cb(void *u)
{
    (void)u;
    unsigned avail = rhead - rtail;
    if (!avail) { ring_empty++; return reof ? 0 : 1; }
    unsigned pos = rtail % RING, n = avail < 2048 ? avail : 2048;
    if (n > RING - pos) n = RING - pos;
    dma_channel_send_normal(DMA_CHANNEL_toIPU, ring + pos, n >> 4, 0, 0);
    rtail += n;
    return 1;
}

/* ---------------- GS ---------------- */
static MPEGSequenceInfo sinfo; static char *pic; static packet_t *xfer, *drawp; static int tex_addr;
static framebuffer_t fb; static zbuffer_t zb; static s64 cur_pts, out_pts;
static void *init_cb(void *u, MPEGSequenceInfo *si)
{
    (void)u; sinfo = *si;
    int sz = si->m_Width * si->m_Height * 4;
    pic = memalign(64, sz); SyncDCache(pic, pic + sz);
    int mbw = si->m_Width >> 4, mbh = si->m_Height >> 4, tbw = (si->m_Width + 63) >> 6;
    int tw = draw_log2(si->m_Width), th = draw_log2(si->m_Height), tp = tex_addr >> 6;
    xfer = packet_init(4 + 6 * mbw * mbh + 8, PACKET_NORMAL);
    qword_t *q = xfer->data;
    DMATAG_CNT(q, 3, 0, 0, 0); q++;
    PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TRXREG(16, 16), GS_REG_TRXREG); q++;
    PACK_GIFTAG(q, GS_SET_BITBLTBUF(0, 0, 0, tp, tbw, GS_PSM_32), GS_REG_BITBLTBUF); q++;
    char *img = pic;
    for (int ly = 0; ly < si->m_Height; ly += 16)
        for (int lx = 0; lx < si->m_Width; lx += 16, img += 1024) {
            DMATAG_CNT(q, 4, 0, 0, 0); q++;
            PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
            PACK_GIFTAG(q, GS_SET_TRXPOS(0, 0, lx, ly, 0), GS_REG_TRXPOS); q++;
            PACK_GIFTAG(q, GS_SET_TRXDIR(0), GS_REG_TRXDIR); q++;
            PACK_GIFTAG(q, GIF_SET_TAG(64, 1, 0, 0, 2, 0), 0); q++;
            DMATAG_REF(q, 64, (unsigned)img, 0, 0, 0); q++;
        }
    xfer->qwc = q - xfer->data;
    int y0 = (SH - si->m_Height) / 2;
    drawp = packet_init(7, PACKET_NORMAL); q = drawp->data;
    PACK_GIFTAG(q, GIF_SET_TAG(6, 1, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TEX0(tp, tbw, GS_PSM_32, tw, th, 1, 1, 0, 0, 0, 0, 0), GS_REG_TEX0_1); q++;
    PACK_GIFTAG(q, GS_SET_PRIM(6, 0, 1, 0, 0, 0, 1, 0, 0), GS_REG_PRIM); q++;
    PACK_GIFTAG(q, GS_SET_UV(0, 0), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 << 4), ((2048 + y0) << 4), 0), GS_REG_XYZ2); q++;
    PACK_GIFTAG(q, GS_SET_UV(si->m_Width << 4, si->m_Height << 4), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ(((2048 + SW) << 4), ((2048 + y0 + si->m_Height) << 4), 0), GS_REG_XYZ2); q++;
    drawp->qwc = q - drawp->data;
    return pic;
}
static void gfx_init(void)
{
    fb.width = SW; fb.height = SH; fb.mask = 0; fb.psm = GS_PSM_32;
    fb.address = graph_vram_allocate(SW, SH, GS_PSM_32, GRAPH_ALIGN_PAGE);
    zb.enable = 0; zb.mask = 0; zb.method = 0; zb.zsm = 0; zb.address = 0;
    dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    graph_initialize(0, SW, SH, GS_PSM_32, 0, 0);
    tex_addr = graph_vram_allocate(0, 0, GS_PSM_32, GRAPH_ALIGN_BLOCK);
    packet_t *pck = packet_init(100, PACKET_NORMAL); qword_t *q = pck->data;
    q = draw_setup_environment(q, 0, &fb, &zb);
    q = draw_clear(q, 0, 0, 0, (float)SW, (float)SH, 0, 0, 0);
    dma_channel_send_normal(DMA_CHANNEL_GIF, pck->data, q - pck->data, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
}

/* ---------------- player ---------------- */
#define MAXF 2048
typedef struct { unsigned short at_ms, dec, rpc, up, work, empty, drift; signed char dd; unsigned char drop; } frec_t;
static frec_t fr[MAXF];
typedef struct { unsigned frames, shown, dropped, dec_max, dec_sum, total_ms; int w, h, fnum, fden, ablocks, under, aerr, stalls; unsigned cause[5]; unsigned gaphist[5]; unsigned nrec; } vstats_t;
static vstats_t vs[2];

static int play(const char *name, vstats_t *st)
{
    char vp[64], ap[64], ip[64];
    snprintf(vp, sizeof vp, "cdrom0:\\%s.M2V;1", name); snprintf(ap, sizeof ap, "cdrom0:\\%s.VAS;1", name); snprintf(ip, sizeof ip, "cdrom0:\\%s.VNF;1", name);
    memset(st, 0, sizeof *st); memset(fr, 0, sizeof fr);
    ring_empty = rd_n = rd_slow = rd_max_us = rd_total_us = 0; rd_ring_min = 0xffffffffu;
    unsigned vi[8] = {0};
    if (!ring) ring = memalign(64, RING);
    int fd = open(ip, O_RDONLY); if (fd < 0) return -1; read(fd, vi, 32); close(fd);
    st->w = vi[1]; st->h = vi[2]; st->fnum = vi[3]; st->fden = vi[4]; st->ablocks = vi[6];
    rfd = open(vp, O_RDONLY); if (rfd < 0) return -2;
    rhead = rtail = 0; reof = 0; rquit = 0;
    ee_thread_t t = {.func = reader, .stack = rstack, .stack_size = sizeof rstack, .gp_reg = &_gp, .initial_priority = 12};
    int tid = CreateThread(&t); StartThread(tid, NULL);
    while (rhead - rtail < 4 * RCHUNK && !reof) usleep(1000); /* prefill */
    int have_audio = st->ablocks > 0;
    const int base = 4096 * 44100 / 48000;
    if (have_audio) sstart(0, base, 0x3000, ap, 0);
    int ss[7] = {0};
    if (have_audio) { for (int i = 0; i < 400; i++) { if (sstat(0, ss) == 5 || ss[0] == 4) break; usleep(5000); } if (ss[0] != 5) have_audio = 0; }
    MPEG_Initialize(data_cb, NULL, init_cb, NULL, &cur_pts);
    unsigned t0 = 0; int started = 0; unsigned n = 0, last_empty = 0; int last_drift = 0;
    const unsigned fnum_ = (unsigned)st->fnum, fden_ = (unsigned)st->fden;
    const int frame_ms = (int)(fden_ * 1000 / fnum_);
    for (;;) {
        unsigned it0 = us();
        SyncDCache(pic ? pic : (char *)ring, (pic ? pic + sinfo.m_Width * sinfo.m_Height * 4 : (char *)ring + 16));
        unsigned a = us();
        if (!MPEG_Picture(pic, &out_pts)) break;
        unsigned dec_us = us() - a; st->dec_sum += dec_us / 1000; if (dec_us / 1000 > st->dec_max) st->dec_max = dec_us / 1000;
        unsigned due = (unsigned)((unsigned long long)n * fden_ * 1000 / fnum_);
        unsigned idx = n; n++; st->frames++;
        if (!started) {
            dma_channel_wait(DMA_CHANNEL_GIF, 0); dma_wait_fast();
            dma_channel_send_chain(DMA_CHANNEL_GIF, xfer->data, xfer->qwc, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0);
            dma_channel_send_normal(DMA_CHANNEL_GIF, drawp->data, drawp->qwc, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0);
            if (have_audio) splay(0);
            t0 = ms(); started = 1; st->shown++; last_empty = ring_empty; continue;
        }
        int now; unsigned rpc_max = 0, wait_us = 0;
        for (;;) {
            if (have_audio) { unsigned r0 = us(); sstat(0, ss); unsigned r = us() - r0; if (r > rpc_max) rpc_max = r; now = (int)((long long)ss[6] * 28 * 1000 / 44100); }
            else now = (int)(ms() - t0);
            if (now >= (int)due) break;
            unsigned w0 = us(); graph_wait_vsync(); wait_us += us() - w0;
        }
        int drift = now - (int)due; /* >0: video late */
        unsigned up_us = 0; int dropped = drift > 2 * frame_ms + 10;
        if (dropped) st->dropped++;
        else {
            unsigned u0 = us();
            dma_channel_wait(DMA_CHANNEL_GIF, 0); dma_wait_fast();
            dma_channel_send_chain(DMA_CHANNEL_GIF, xfer->data, xfer->qwc, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0);
            dma_channel_send_normal(DMA_CHANNEL_GIF, drawp->data, drawp->qwc, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0);
            up_us = us() - u0; st->shown++;
        }
        unsigned work = us() - it0 - wait_us;
        if (idx < MAXF) {
            frec_t *f = &fr[idx];
            f->at_ms = now; f->dec = dec_us / 1000; f->rpc = rpc_max / 1000; f->up = up_us / 1000; f->work = work / 1000;
            f->empty = ring_empty - last_empty; f->drift = drift < 0 ? 0 : drift; f->dd = (signed char)(drift - last_drift > 127 ? 127 : drift - last_drift < -128 ? -128 : drift - last_drift); f->drop = dropped;
            st->nrec = idx + 1;
        }
        last_empty = ring_empty; last_drift = drift;
    }
    st->total_ms = ms() - t0;
    if (have_audio) { sstat(0, ss); st->under = ss[2]; st->aerr = ss[3]; }
    rquit = 1; MPEG_Destroy();
    if (have_audio) sstop(0);
    close(rfd);
    free(pic); pic = NULL; packet_free(xfer); packet_free(drawp);
    /* analysis: a stall = the video got >=25 ms later vs the audio clock within one iteration */
    for (unsigned i = 1; i < st->nrec; i++) {
        frec_t *f = &fr[i];
        if (f->dd >= 25) {
            st->stalls++;
            if (f->rpc >= 15) st->cause[0]++;          /* sound-server RPC (IOP busy) */
            else if (f->empty) st->cause[1]++;         /* disc ring ran dry */
            else if (f->dec >= 20) st->cause[2]++;     /* decode */
            else if (f->up >= 12) st->cause[3]++;      /* GS upload/draw */
            else st->cause[4]++;                       /* other: emulator/host hiccup, unaccounted */
        }
    }
    return 0;
}

static int report_pending;
static void report(int k, const char *nm, vstats_t *st, int rc)
{
    scr_printf("%s rc=%d %dx%d %d/%d: decoded %u shown %u dropped %u, total %u ms\n", nm, rc, st->w, st->h, st->fnum, st->fden, st->frames, st->shown, st->dropped, st->total_ms);
    scr_printf(" decode avg %u max %u ms | audio stream under=%d err=%d\n", st->frames ? st->dec_sum / st->frames : 0, st->dec_max, st->under, st->aerr);
    scr_printf(" disc reader: %u reads, avg %u ms, max %u ms, >30ms: %u, ring min fill %u KB, ring-empty calls %u\n", rd_n, rd_n ? rd_total_us / rd_n / 1000 : 0, rd_max_us / 1000, rd_slow,
               rd_ring_min == 0xffffffffu ? 0 : rd_ring_min / 1024, ring_empty);
    scr_printf(" stalls (video +25ms later vs audio in one frame): %d  rpc=%u ring=%u decode=%u gs=%u other=%u\n", st->stalls, st->cause[0], st->cause[1], st->cause[2], st->cause[3], st->cause[4]);
    int lines = 0;
    for (unsigned i = 1; i < st->nrec && lines < 10; i++) {
        frec_t *f = &fr[i];
        if (f->dd >= 25 || f->drop) { scr_printf("  #%u t=%ums drift=%u dd=%d dec=%u rpc=%u gs=%u work=%u ring0=%u%s\n", i, f->at_ms, f->drift, f->dd, f->dec, f->rpc, f->up, f->work, f->empty, f->drop ? " DROP" : ""); lines++; }
    }
    (void)k;
}

int main(void)
{
    SifInitRpc(0);
    SifLoadFileInit();
    sceCdInit(SCECdINIT);
    for (int i = 0; i < 200 && sceCdDiskReady(0) == SCECdNotReady; i++) usleep(50000);
    sbv_patch_enable_lmb();
    int m1, m2;
    SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &m1);
    SifExecModuleBuffer(engsnd_irx, size_engsnd_irx, 0, NULL, &m2);
    for (int tries = 0; tries < 200; tries++) { if (SifBindRpc(&cd, DEV, 0) >= 0 && cd.server) break; usleep(20000); }
    buf = memalign(64, 0x10000);
    call(C_INIT, 4);
    ChangeThreadPriority(GetThreadId(), 8);
    gfx_init();
    static const char *names[] = {"CH3EN", "CH5EN"};
    int rc[2];
#ifdef ONLY5
    rc[1] = play(names[1], &vs[1]);
    init_scr(); report(1, names[1], &vs[1], rc[1]);
#elif defined(ONLY3)
    rc[0] = play(names[0], &vs[0]);
    init_scr(); report(0, names[0], &vs[0], rc[0]);
#else
    rc[0] = play(names[0], &vs[0]);
    unsigned rn0 = rd_n, rs0 = rd_slow, rm0 = rd_max_us, re0 = ring_empty; (void)rn0; (void)rs0; (void)rm0; (void)re0;
    report_pending = 1;
    static vstats_t keep; keep = vs[0]; static frec_t fkeep[MAXF]; memcpy(fkeep, fr, sizeof fr);
    rc[1] = play(names[1], &vs[1]);
    init_scr();
    frec_t *save = fkeep; (void)save;
    report(0, names[0], &keep, rc[0]);
    report(1, names[1], &vs[1], rc[1]);
#endif
    scr_printf("done.\n");
    SleepThread();
    return 0;
}
