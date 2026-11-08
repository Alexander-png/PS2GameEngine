// gmrun: runs a converted GameMaker game (generated code from converter/gml2c.py + a GMPK v4 pack) on the PS2.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <debug.h>
#include <graph.h>
#include "ge/gfx.h"
#include "ge/io.h"
#include "ge/log.h"
#include "ge/input.h"
#include "ge/room.h"
#include "gml/gml_rt.h"

#ifndef EXIT_SECS
#define EXIT_SECS 0                     /* 0 = run until SELECT+START */
#endif
#ifndef START_ROOM
#define START_ROOM 0
#endif
void gml_draw_init(ge_pack_t *p);
int gmrun_audio_init(void);

/* ---- input: pad -> GameMaker virtual keys ---- */
static unsigned pad_now, pad_prev;
static unsigned vk_mask(int vk)
{
    switch (vk) {
    case 37: return GE_BTN_LEFT;
    case 38: return GE_BTN_UP;
    case 39: return GE_BTN_RIGHT;
    case 40: return GE_BTN_DOWN;
    case 90: return GE_BTN_CROSS;       /* Z: confirm */
    case 88: return GE_BTN_CIRCLE;      /* X: cancel */
    case 67: return GE_BTN_TRIANGLE;    /* C: menu */
    case 13: return GE_BTN_START;       /* Enter */
    case 16: return GE_BTN_L1;          /* Shift */
    case 17: return GE_BTN_R1;          /* Ctrl */
    default: return 0;
    }
}
static unsigned key_state(int vk)
{
    unsigned m = vk_mask(vk);
    if (!m) return 0;
    unsigned down = (pad_now & m) != 0, was = (pad_prev & m) != 0;
    return down | ((down && !was) << 1) | ((!down && was) << 2);
}

/* ---- save files: HDD when available, always mirrored in RAM so a disc/emulator run still behaves ---- */
typedef struct { char *name; void *data; int len; } ramfile;
static ramfile rf[64];
static int nrf;
static ramfile *rf_find(const char *name) { for (int i = 0; i < nrf; i++) if (!strcmp(rf[i].name, name)) return &rf[i]; return NULL; }
static void log_tail(const char *tag, const char *name, const unsigned char *d, int len)
{
    if (strncmp(name, "file", 4) || len < 40) return;
    char h[160]; int n = 0;
    for (int k = len - 40; k < len; k++) n += snprintf(h + n, sizeof h - n, "%02x", d[k]);
    ge_log("%s %s len=%d tail %s", tag, name, len, h);
}
static void *fs_read(const char *name, int *len)
{
    ramfile *r = rf_find(name);
    ge_log("fs_read %s ram=%d", name, r ? r->len : -1);
    if (r) {
        if (r->len <= 0) return NULL;
        void *b = malloc(r->len + 1);
        memcpy(b, r->data, r->len);
        *len = r->len;
        return b;
    }
    char path[128];
    snprintf(path, sizeof path, "save/%s", name);
    int sz = 0;
    void *d = ge_io_read_all(path, &sz);
    ge_log("fs_read %s hdd=%d", path, d ? sz : -1);
    if (!d) return NULL;
    void *b = malloc(sz + 1);
    memcpy(b, d, sz);
    ge_io_free(d);
    *len = sz;
    log_tail("read", name, b, sz);
    return b;
}
static int fs_write(const char *name, const void *data, int len)
{
    if (len > 0) log_tail("write", name, data, len);
    ramfile *r = rf_find(name);
    if (!r) {
        if (nrf == 64) return -1;
        r = &rf[nrf++];
        r->name = strdup(name); r->data = NULL; r->len = 0;
    }
    free(r->data);
    r->len = len < 0 ? 0 : len;
    r->data = r->len ? malloc(r->len) : NULL;
    if (r->len) memcpy(r->data, data, r->len);
    char path[128];
    snprintf(path, sizeof path, "save/%s", name);
    /* PFS/fileXio on real hardware lost the unaligned tail of a file when reading it back (a save lost its last
     * 9 bytes: the room number): pad the on-disk copy with blank lines up to a multiple of 512 (parsers skip them) */
    int plen = r->len ? (r->len + 511) & ~511 : 0;
    char *pad = plen ? malloc(plen) : NULL;
    if (pad) { memcpy(pad, data, r->len); memset(pad + r->len, '\n', plen - r->len); }
    int wr = ge_io_write(path, pad ? pad : "", plen);          /* fails on disc: RAM copy only */
    if (pad && wr == 0 && !strncmp(name, "file", 4)) {          /* read it straight back and report the first difference */
        int sz2 = 0; unsigned char *chk = ge_io_read_all(path, &sz2);
        int bad = -1;
        if (!chk || sz2 != plen) bad = -2;
        else for (int k = 0; k < plen; k++) if (chk[k] != (unsigned char)pad[k]) { bad = k; break; }
        ge_log("verify %s: size %d/%d first diff %d", name, sz2, plen, bad);
        if (chk) ge_io_free(chk);
    }
    free(pad);
    ge_log("fs_write %s len=%d -> %d", name, len, wr);
    return 0;
}
static void hk_log(const char *m) { ge_log("%s", m); }


/* ---- profiling helpers ---- */
#include <malloc.h>
#define CYC_US 295u
static inline unsigned cyc(void) { unsigned c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
extern unsigned (*gm_prof_cyc)(void);
extern void gm_prof_dump(void (*out)(const char *, unsigned long long, unsigned));
static void prof_out(const char *name, unsigned long long c, unsigned n) { ge_log("  draw-prof %s: %llu us/5s over %u calls (%llu us per call)", name, c / CYC_US, n, n ? c / CYC_US / n : 0); }
typedef struct { unsigned step, draw, gs; int inst; } frame_t;
typedef struct { unsigned n, step_sum, step_max, draw_sum, draw_max, gs_sum, gs_max, inst_max; } prof_t;
static void prof_add(prof_t *p, const frame_t *f)
{
    p->n++; p->step_sum += f->step; p->draw_sum += f->draw; p->gs_sum += f->gs;
    if (f->step > p->step_max) p->step_max = f->step;
    if (f->draw > p->draw_max) p->draw_max = f->draw;
    if (f->gs > p->gs_max) p->gs_max = f->gs;
    if ((unsigned)f->inst > p->inst_max) p->inst_max = f->inst;
}
static unsigned gs_cyc;
static void (*orig_draw_end)(void);
void gml_draw_debug(int x, int y, const char *str);
static int overlay =
#ifdef OVERLAY_ON
    1;
#else
    0;
#endif
static char ov_line[2][96];
static void timed_draw_end(void)
{
    if (overlay) { gml_draw_debug(2, 2, ov_line[0]); gml_draw_debug(2, 12, ov_line[1]); }
    unsigned c = cyc(); if (orig_draw_end) orig_draw_end(); gs_cyc += cyc() - c;   /* frame_end waits for the GS to finish */
}
/* ---- log segments: one file per game area (and per ~200 KB), so a long run stays readable and the log never fills up ---- */
static const char *area_of(const char *rn)
{
    static const char *const pre[][2] = {
        {"room_tundra", "snowdin"}, {"room_fogroom", "snowdin"}, {"room_icecave", "snowdin"}, {"room_ice_dog", "snowdin"}, {"room_papdate", "snowdin"},
        {"room_water", "waterfall"}, {"room_fire", "hotland"}, {"room_kitchen", "hotland"},
        {"room_castle", "castle"}, {"room_basement", "castle"}, {"room_asgore", "castle"}, {"room_truelab", "truelab"}, {"room_end", "end"},
        {"room_ruins", "ruins"}, {"room_torhouse", "ruins"}, {"room_torielroom", "ruins"}, {"room_lastruins", "ruins"},
    };
    for (unsigned i = 0; i < sizeof pre / sizeof pre[0]; i++) if (!strncmp(rn, pre[i][0], strlen(pre[i][0]))) return pre[i][1];
    return NULL;                                    /* battle, shops, menus ...: stay in the current area */
}
static const char *cur_area = "ruins";
static int area_part = 1;
static void log_area_check(int room)
{
    const char *a = room >= 0 ? area_of(gm_rooms[room].name) : NULL;
    if (a && strcmp(a, cur_area)) { cur_area = a; area_part = 1; ge_log_segment(a); }
    else if (ge_log_size() > 200 * 1024) { char t[40]; snprintf(t, sizeof t, "%s%d", cur_area, ++area_part); ge_log_segment(t); }
}
/* battle state for the 5 s log line: lets a log show where a fight sits (the globals the battle state machine uses, the monster's own state and alarms, every instance) */
static double gnum(const char *n) { int v = gm_vid(n); return v < 0 ? -999 : (double)gm_to_real(gm_get(gm_global_inst, v)); }
static double inum(gm_inst *i, const char *n) { int v = gm_vid(n); return v < 0 || !i ? -999 : (double)gm_to_real(gm_get(i, v)); }
static void log_battle_state(void)
{
    ge_log("  battle: mnfight %g myfight %g turntimer %g bmenuno %g hp %g", gnum("mnfight"), gnum("myfight"), gnum("turntimer"), gnum("bmenuno"), gnum("hp"));
    gm_inst *m = gm_obj1(gm_find_object("obj_maddummy"));
    if (m) ge_log("  maddummy: con %g con2 %g hp %g hurtsies %g talked %g attacked %g next %g speed %g pos %g,%g | alarms %d %d %d %d %d %d %d %d %d %d %d %d", inum(m, "con"), inum(m, "con2"), inum(m, "hp"), inum(m, "hurtsies"),
                  inum(m, "talked"), inum(m, "attacked"), inum(m, "next"), inum(m, "speed"), (double)m->x, (double)m->y, m->alarm[0], m->alarm[1], m->alarm[2], m->alarm[3], m->alarm[4], m->alarm[5], m->alarm[6], m->alarm[7], m->alarm[8], m->alarm[9], m->alarm[10], m->alarm[11]);
    {   /* per-object instance counts (compact) */
        static unsigned short cnt[4096]; char buf[400]; int n = 0, total = gm_instance_count(-1);
        if (gm_nobjs <= 4096) {
            memset(cnt, 0, sizeof cnt);
            for (int k = 0; k < total; k++) { gm_inst *i = gm_instance_nth(-1, k); if (i->obj >= 0 && i->obj < 4096) cnt[i->obj]++; }
            buf[0] = 0;
            for (int o = 0; o < gm_nobjs && n < 370; o++) if (cnt[o]) n += snprintf(buf + n, sizeof buf - n, "%s=%d ", gm_objs[o].name, cnt[o]);
            ge_log("  instances(%d): %s", total, buf);
        }
    }
}
static int heap_used(void) { struct mallinfo mi = mallinfo(); return mi.uordblks; }
static void log_room_summary(const char *what, int r, const prof_t *p, unsigned ms, const unsigned *u0)
{
    if (!p->n) return;
    unsigned st[4]; ge_gfx_stats(&st[0], &st[1], &st[2], &st[3]);
    ge_log("room %s %s: %u frames in %u ms (%.1f fps) | step avg %u max %u us | draw avg %u max %u us | gs avg %u max %u us | max inst %u | tex +%u uploads +%u evict +%u KB",
           what, r >= 0 ? gm_rooms[r].name : "?", p->n, ms, p->n * 1000.0f / (ms ? ms : 1), p->step_sum / p->n, p->step_max, p->draw_sum / p->n, p->draw_max,
           p->gs_sum / p->n, p->gs_max, p->inst_max, st[0] - u0[0], st[1] - u0[1], (st[2] - u0[2]) / 1024);
    int top[3] = {-1, -1, -1}, topn[3] = {0, 0, 0};
    for (int o = 0; o < gm_nobjs; o++) {
        int c = gm_instance_count(o);
        for (int k = 0; k < 3; k++) if (c > topn[k]) { for (int j = 2; j > k; j--) { top[j] = top[j - 1]; topn[j] = topn[j - 1]; } top[k] = o; topn[k] = c; break; }
    }
    if (topn[0] > 0) ge_log("  most instances now: %s x%d, %s x%d, %s x%d", top[0] >= 0 ? gm_objs[top[0]].name : "-", topn[0], top[1] >= 0 ? gm_objs[top[1]].name : "-", topn[1], top[2] >= 0 ? gm_objs[top[2]].name : "-", topn[2]);
}

int main(void)
{
#ifdef HDD_BOOT
    int io_r = ge_io_init("hdd0:+DEVGAME", "/current");
#else
    int io_r = ge_io_init(NULL, NULL);
#endif
    ge_log_init("gmrun", 512 * 1024);
    ge_log("storage init result %d, source %s", io_r, ge_io_source());
    int pad_r = ge_input_init();
    ge_log("pad init result %d", pad_r);
    ge_gfx_init(640, 480);
    ge_pack_t *p = ge_pack_open("GAME.GMP");
    if (!p) { ge_log("pack open failed"); ge_log_save(); init_scr(); scr_printf("pack open failed\n"); for (;;) sleep(1); }
    ge_log("pack: %d rooms, %d sprites, %d fonts", ge_pack_room_count(p), ge_pack_sprite_count(p), ge_pack_font_count(p));
    gm_hk.key_state = key_state;
    gm_hk.fs_read = fs_read;
    gm_hk.fs_write = fs_write;
    gm_hk.log = hk_log;
    gml_draw_init(p);
    gmrun_audio_init();
    gm_init();
#ifdef BOOT_SCRIPT      /* dev shortcut: run a game script (e.g. SCR_GAMESTART) so globals exist when starting in a gameplay room */
    for (int q = 0; q < gm_nscripts; q++) if (gm_scripts[q].fn && !strcmp(gm_scripts[q].name, BOOT_SCRIPT)) {
        int ot = gm_find_object("obj_time"); gm_inst *t = gm_obj1(ot); if (!t) t = gm_instance_create(0, 0, ot);
        gm_scripts[q].fn(t, t, 0, NULL);
    }
#endif
    gm_goto_room(START_ROOM);
    ge_log("start: room %d, %d objects", START_ROOM, gm_nobjs);
    ge_log_save();
    unsigned next = ge_time_ms(), t_end = EXIT_SECS ? ge_time_ms() + EXIT_SECS * 1000u : 0;

    /* ---- profiling: EE cycle counter (COP0 Count, 294.912 MHz) around step / draw / GS wait; summaries per room and every 5 s ---- */
    prof_t win = {0}, room = {0};
    unsigned t_log = ge_time_ms(), t_save = t_log, frames = 0, late = 0, slow = 0;
    int cur_room = gm_room_cur;
    int ema_step = 0, ema_draw = 0, ema_gs = 0; float fps_now = 0;
    unsigned room_t0 = ge_time_ms();
    unsigned us_u[4]; ge_gfx_stats(&us_u[0], &us_u[1], &us_u[2], &us_u[3]);
    unsigned rm_u[4]; memcpy(rm_u, us_u, sizeof rm_u);
    gm_prof_cyc = cyc;
    orig_draw_end = gm_hk.draw_end;
    gm_hk.draw_end = timed_draw_end;
    for (;;) {
        unsigned now = ge_time_ms();
        if (t_end && now >= t_end) break;
        if (now < next) { graph_wait_vsync(); continue; }
        unsigned step_ms = 1000u / (unsigned)(gm_room_speed > 0 ? gm_room_speed : 30);
        if (now > next + step_ms) late++;                       /* the previous frame took longer than one game step */
        next += step_ms;
        if (now > next + 250) next = now;
        pad_prev = pad_now;
        pad_now = ge_input_poll();
        if ((pad_now & (GE_BTN_SELECT | GE_BTN_START)) == (GE_BTN_SELECT | GE_BTN_START)) break;
        unsigned c0 = cyc();
        gm_step();
        unsigned c1 = cyc();
        gs_cyc = 0;
        gm_draw();
        unsigned c2 = cyc();
        frame_t f = { (c1 - c0) / CYC_US, (c2 - c1) / CYC_US, gs_cyc / CYC_US, gm_instance_count(-1) };
        frames++;
        prof_add(&win, &f); prof_add(&room, &f);
        if ((pad_now & GE_BTN_SELECT) && !(pad_prev & GE_BTN_SELECT) && !(pad_now & GE_BTN_START)) overlay ^= 1;
        if (overlay && (frames & 7) == 0) {                          /* refresh the on-screen numbers (SELECT toggles them) */
            ema_step += ((int)f.step - ema_step) / 8; ema_draw += ((int)f.draw - ema_draw) / 8; ema_gs += ((int)f.gs - ema_gs) / 8;
            snprintf(ov_line[0], sizeof ov_line[0], "S %d.%d D %d.%d GS %d.%d ms I %d", ema_step / 1000, ema_step / 100 % 10, ema_draw / 1000, ema_draw / 100 % 10, ema_gs / 1000, ema_gs / 100 % 10, f.inst);
            snprintf(ov_line[1], sizeof ov_line[1], "%s %.1ffps late %u", gm_room_cur >= 0 ? gm_rooms[gm_room_cur].name : "?", fps_now, late);
        }
        if (f.step + f.draw > step_ms * 1000u * 3 / 2) {          /* slow frame: more than 1.5 game steps */
            slow++;
            if (slow <= 40) ge_log("slow frame %u: room %s step %u us draw %u us (gs wait %u us) inst %d", frames, gm_room_cur >= 0 ? gm_rooms[gm_room_cur].name : "?", f.step, f.draw, f.gs, f.inst);
        }
        {   /* log view-size changes (zoom cutscenes) */
            extern int gm_camera_view(int *w, int *h); static int lw = -1, lh = -1; int vw = 0, vh = 0;
            if (gm_camera_view(&vw, &vh) && (vw != lw || vh != lh)) { ge_log("view0 size %d x %d (room %s)", vw, vh, gm_room_cur >= 0 ? gm_rooms[gm_room_cur].name : "?"); lw = vw; lh = vh; }
        }
        if (gm_room_cur != cur_room) {                            /* room changed during this step: log the room we left and the cost of the switch */
            log_room_summary("left", cur_room, &room, ge_time_ms() - room_t0, rm_u);
            ge_log("room change %s -> %s: step %u us (load), %d instances, %d bytes malloc'd", cur_room >= 0 ? gm_rooms[cur_room].name : "?",
                   gm_room_cur >= 0 ? gm_rooms[gm_room_cur].name : "?", f.step, f.inst, heap_used());
            cur_room = gm_room_cur; log_area_check(cur_room); memset(&room, 0, sizeof room); room_t0 = ge_time_ms();
            ge_gfx_stats(&rm_u[0], &rm_u[1], &rm_u[2], &rm_u[3]);
            if (ge_time_ms() - t_save > 1500) { ge_log_save(); t_save = ge_time_ms(); }
        }
        if (ge_time_ms() - t_log >= 5000) {
            unsigned el = ge_time_ms() - t_log;
            float win_fps = frames * 1000.0f / el;
            ge_log("fps %.1f (%u frames, %u late, %u slow total) room %s inst %d heap %d | step avg %u max %u us | draw avg %u max %u us | gs wait avg %u max %u us",
                   frames * 1000.0f / el, win.n, late, slow, gm_room_cur >= 0 ? gm_rooms[gm_room_cur].name : "?", gm_instance_count(-1), heap_used(),
                   win.n ? win.step_sum / win.n : 0, win.step_max, win.n ? win.draw_sum / win.n : 0, win.draw_max, win.n ? win.gs_sum / win.n : 0, win.gs_max);
            gm_prof_dump(prof_out);
            if (gm_room_cur >= 0 && !strcmp(gm_rooms[gm_room_cur].name, "room_battle")) log_battle_state();
            unsigned st[4]; ge_gfx_stats(&st[0], &st[1], &st[2], &st[3]);
            ge_log("  textures: +%u uploads +%u evictions +%u KB +%u cache hits", st[0] - us_u[0], st[1] - us_u[1], (st[2] - us_u[2]) / 1024, st[3] - us_u[3]);
            memcpy(us_u, st, sizeof us_u);
            memset(&win, 0, sizeof win); frames = 0; late = 0; t_log = ge_time_ms();
            fps_now = win_fps; ge_log_save(); t_save = t_log;
        }
    }
    log_room_summary("final", cur_room, &room, ge_time_ms() - room_t0, rm_u);
    ge_log("exit: %u frames in last window, %u slow frames in total", frames, slow);
    ge_log_save();
    ge_input_shutdown();
    ge_io_shutdown();
    return 0;
}
