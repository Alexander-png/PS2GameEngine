// mpegtest: hardware test for the ps2sdk libmpeg D3/D4 QWC fix (see docs/ps2sdk_libmpeg_issue_draft.md).
// Decodes two synthetic MPEG-2 streams (ffmpeg testsrc2, I/P only, 60 frames each) from memory with the IPU:
//   s352: 352x288 = 396 macroblocks per picture  (control: always worked)
//   s640: 640x480 = 1200 macroblocks per picture (> 1023: the case that hangs / corrupts without the fix)
// Shows the frames, logs per-frame average colour + decode time, and a watchdog logs "HANG" if decoding stalls.
// The log goes to hdd0:+DEVGAME:/current/logs (uploaded by PS2Deployer). Build with ORIG=1 to link the unpatched upstream libmpeg.
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "ge/io.h"
#include "ge/log.h"

extern unsigned char s352_m2v[], s640_m2v[];
extern unsigned int size_s352_m2v, size_s640_m2v;

#define SW 640
#define SH 480
#define EXPECT_FRAMES 59 /* libmpeg hands out 59 of the 60 pictures of each test stream, also for the 352x288 control (the last picture is not delivered) */

/* -DNOSAVE: no HDD writes while decoding (log lines stay in RAM and are written once at the end), to rule out the log writes disturbing the test */
#ifdef NOSAVE
#define SAVE_MID() ((void)0)
#else
#define SAVE_MID() ge_log_save()
#endif
static const unsigned char *src; static unsigned src_len, src_pos;
static volatile int eof_logged;
static volatile unsigned progress; /* bumped on every decoded picture and every data callback */

static int data_cb(void *u)
{
    (void)u;
    progress++;
    if (src_pos >= src_len) { if (!eof_logged) { eof_logged = 1; ge_log("data_cb: end of stream reached (%u bytes sent)", src_pos); SAVE_MID(); } return 0; }
    unsigned n = src_len - src_pos; if (n > 2048) n = 2048;
    dma_channel_send_normal(DMA_CHANNEL_toIPU, (void *)(src + src_pos), n >> 4, 0, 0);
    src_pos += n;
    return 1;
}

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
    int y0 = (SH - si->m_Height) / 2, x0 = (SW - si->m_Width) / 2;
    drawp = packet_init(7, PACKET_NORMAL); q = drawp->data;
    PACK_GIFTAG(q, GIF_SET_TAG(6, 1, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TEX0(tp, tbw, GS_PSM_32, tw, th, 1, 1, 0, 0, 0, 0, 0), GS_REG_TEX0_1); q++;
    PACK_GIFTAG(q, GS_SET_PRIM(6, 0, 1, 0, 0, 0, 1, 0, 0), GS_REG_PRIM); q++;
    PACK_GIFTAG(q, GS_SET_UV(0, 0), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ(((2048 + x0) << 4), ((2048 + y0) << 4), 0), GS_REG_XYZ2); q++;
    PACK_GIFTAG(q, GS_SET_UV(si->m_Width << 4, si->m_Height << 4), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ(((2048 + x0 + si->m_Width) << 4), ((2048 + y0 + si->m_Height) << 4), 0), GS_REG_XYZ2); q++;
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

/* ---------------- watchdog ---------------- */
static volatile int wd_stream = -1;
static char wd_stack[16384] __attribute__((aligned(16)));
static void watchdog(void *a)
{
    (void)a;
    unsigned last = progress; int idle = 0;
    for (;;) {
        usleep(500 * 1000);
        if (progress != last) { last = progress; idle = 0; continue; }
        if (wd_stream >= 0 && ++idle == 16) { /* 8 s without progress */
            ge_log("HANG: stream %d made no progress for 8 s (IPU_CTRL=0x%08x)", wd_stream, *(volatile unsigned *)0x10002010);
            SAVE_MID();
        }
    }
}

static unsigned us(void) { return (unsigned)((unsigned long long)clock() * 1000000ull / CLOCKS_PER_SEC); }

static int run(int id, const char *name, const unsigned char *data, unsigned len)
{
    /* the IPU reads whole quadwords; ffmpeg's raw stream has no sequence end code, so append one plus zero padding */
    unsigned total = (len + 4 + 64 + 15) & ~15u;
    unsigned char *copy = memalign(64, total);
    memset(copy, 0, total); memcpy(copy, data, len);
    copy[len + 2] = 1; copy[len + 3] = 0xB7;
    SyncDCache(copy, copy + total);
    src = copy; src_len = total; src_pos = 0; eof_logged = 0;
    pic = NULL; wd_stream = id; progress++;
    ge_log("stream %s: %u bytes, start", name, len); SAVE_MID();
    MPEG_Initialize(data_cb, NULL, init_cb, NULL, &cur_pts);
    int n = 0; unsigned dec_sum = 0, dec_max = 0;
    for (;;) {
        SyncDCache(pic ? pic : (char *)copy, pic ? pic + sinfo.m_Width * sinfo.m_Height * 4 : (char *)copy + 16);
        unsigned a = us();
        if (!MPEG_Picture(pic, &out_pts)) break;
        unsigned d = us() - a; dec_sum += d; if (d > dec_max) dec_max = d;
        progress++;
        if (n == 0) ge_log("stream %s: sequence %dx%d (%d MBs per picture)", name, sinfo.m_Width, sinfo.m_Height, (sinfo.m_Width >> 4) * (sinfo.m_Height >> 4));
        /* average colour of a 1/49 sample of the picture: lets the host compare against a reference decode */
        unsigned long r = 0, g = 0, b = 0, cnt = 0, h = 0;
        for (int y = 0; y < sinfo.m_Height; y += 7)
            for (int x = 0; x < sinfo.m_Width; x += 7) {
                const unsigned char *p = (const unsigned char *)pic + (y * sinfo.m_Width + x) * 4;
                r += p[0]; g += p[1]; b += p[2]; cnt++; h = h * 31 + p[0] + 3 * p[1] + 5 * p[2];
            }
        ge_log("frame %s %3d avg %3lu %3lu %3lu hash %08x dec %u us", name, n, r / cnt, g / cnt, b / cnt, (unsigned)h, d);
        dma_channel_wait(DMA_CHANNEL_GIF, 0); dma_wait_fast();
        dma_channel_send_chain(DMA_CHANNEL_GIF, xfer->data, xfer->qwc, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0);
        dma_channel_send_normal(DMA_CHANNEL_GIF, drawp->data, drawp->qwc, 0, 0); dma_channel_wait(DMA_CHANNEL_GIF, 0);
        graph_wait_vsync();
        n++;
        if (n >= 40 && (n % 4) == 0) SAVE_MID(); /* keep the log current in case the next picture hangs */
        if (n > 4 * EXPECT_FRAMES) break; /* garbage pictures without consuming input */
    }
    ge_log("stream %s: MPEG_Picture returned 0 after %d frames", name, n); SAVE_MID();
    MPEG_Destroy();
    wd_stream = -1;
    int ok = n == EXPECT_FRAMES;
    ge_log("RESULT %s: %d frames decoded (expected %d), decode avg %u us max %u us => %s", name, n, EXPECT_FRAMES, n ? dec_sum / n : 0, dec_max, ok ? "PASS" : "FAIL");
    SAVE_MID();
    free(pic); pic = NULL; packet_free(xfer); packet_free(drawp); free(copy);
    return ok;
}

int main(void)
{
    int io_r = ge_io_init("hdd0:+DEVGAME", "/current");
    ge_log_init("mpegtest", 256 * 1024);
#ifdef NOSAVE
    ge_log("(NOSAVE build: log written only at the end)");
#endif
#ifdef ORIG
    ge_log("mpegtest, UNPATCHED upstream libmpeg; storage init %d (%s)", io_r, ge_io_source());
#else
    ge_log("mpegtest, libmpeg with the D3/D4 QWC fix; storage init %d (%s)", io_r, ge_io_source());
#endif
    ge_log_save();
    ee_thread_t t = {.func = watchdog, .stack = wd_stack, .stack_size = sizeof wd_stack, .gp_reg = &_gp, .initial_priority = 4};
    StartThread(CreateThread(&t), NULL);
    gfx_init();
    int ok1 = run(0, "s352", s352_m2v, size_s352_m2v);
    int ok2 = run(1, "s640", s640_m2v, size_s640_m2v);
    ge_log("SUMMARY: 352x288 %s, 640x480 %s", ok1 ? "PASS" : "FAIL", ok2 ? "PASS" : "FAIL");
    ge_log_save(); ge_io_shutdown();
    init_scr();
    scr_printf("mpegtest %s: 352x288 %s, 640x480 %s\nlog saved; run PS2Deployer to upload it.\n",
#ifdef ORIG
               "UNPATCHED",
#else
               "patched",
#endif
               ok1 ? "PASS" : "FAIL", ok2 ? "PASS" : "FAIL");
    SleepThread();
    return 0;
}
