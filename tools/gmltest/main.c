#include <time.h>
/* gmltest: host harness - links the converter output with the GML runtime and runs object code without graphics.
 * usage: gmltest <frames> <object name>...   (creates the objects, then runs N frames) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gml/gml_rt.h"

#ifdef GFX
#include "ge/gfx.h"
#include "ge/io.h"
#include "ge/log.h"
#include "ge/room.h"
void gml_draw_init(ge_pack_t *p);
void ge_gfx_host_dump(const char *path);
static int shotf[16]; static const char *shotp[16]; static int nshot;
#endif
static const char *dumpobj, *gvars[16]; static int ngv;
static const char *trv[4]; static int ntr; static double trlast[4];
static void trace_vars(void)
{
    if (getenv("GT_SEEN")) { static char seen[4096]; for (int k = 0, n = gm_instance_count(-1); k < n; k++) { gm_inst *i = gm_instance_nth(-1, k); if (i->obj < 4096 && !seen[i->obj]) { seen[i->obj] = 1; printf("  [f%llu] first %s\n", (unsigned long long)gm_frame, gm_objs[i->obj].name); } } }   /* GT_SEEN=1: print each object the first time an instance of it exists */
    if (getenv("GT_CNT") && gm_frame % 10 == 0) printf("  [f%llu] count(obj %d)=%d\n", (unsigned long long)gm_frame, atoi(getenv("GT_CNT")), gm_instance_count(atoi(getenv("GT_CNT"))));
    static double tol = -12345; const char *te = getenv("GT_TO");     /* GT_TO=<object>:<var> traces an instance variable */
    if (te) { char on[64], vn[64]; if (sscanf(te, "%63[^:]:%63s", on, vn) == 2) { gm_inst *o = gm_obj1(gm_find_object(on)); int v = gm_vid(vn);
        if (o && v >= 0) { double x = gm_to_real(gm_get(o, v)); if (x != tol) { printf("  [f%llu] %s.%s: %g -> %g\n", (unsigned long long)gm_frame, on, vn, tol, x); tol = x; } } } }
    for (int q = 0; q < ntr; q++) { int v = gm_vid(trv[q]); if (v < 0) continue; double x = gm_to_real(gm_get(gm_global_inst, v)); if (x != trlast[q]) { printf("  [f%llu] %s: %g -> %g\n", (unsigned long long)gm_frame, trv[q], trlast[q], x); trlast[q] = x; } }
}
static int pvk[8], npvk, pper = 20, hvk[8], nhvk;
static int tapvk, tapph;   /* tap=<vk>: pressed (phase 1), held (2), released (3) over three steps */
static int drawstep;       /* draw=1: also run the Draw events every step (UT counts turntimer down in obj_battlecontroller Draw) */
static void gstep(void);
static unsigned keys(int vk)
{
    if (tapvk && vk == tapvk) return tapph == 1 ? 3 : (tapph == 2 ? 1 : 4);
    for (int k = 0; k < nhvk; k++) if (hvk[k] == vk) return 1 | (gm_frame == 1 ? 2 : 0);
    for (int k = 0; k < npvk; k++) if (pvk[k] == vk) {
        int f = (int)(gm_frame % pper);
        return (f < 2 ? 1 : 0) | (f == 0 ? 2 : 0) | (f == 2 ? 4 : 0);
    }
    return 0;
}
static void gstep(void)
{
    gm_step();
#ifdef GFX
    if (drawstep) gm_draw();
#endif
}
int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 1;
    gm_hk.key_state = keys;
    gm_init();
#ifdef GFX
    for (int k = 2; k < argc; k++) if (!strncmp(argv[k], "pack=", 5)) {
        char d[512]; snprintf(d, sizeof d, "%s", argv[k] + 5);
        char *sl = strrchr(d, '/'); char nmb[256]; snprintf(nmb, sizeof nmb, "%s", sl ? sl + 1 : d); const char *nm = nmb; if (sl) { sl[1] = 0; } else d[0] = 0;
        ge_io_init(NULL, d); ge_log_init("gmltest", 65536); ge_gfx_init(640, 480);
        ge_pack_t *p = ge_pack_open(nm);
        if (!p) { printf("pack open failed\n"); return 1; }
        gml_draw_init(p);
    }
#endif
    printf("objects %d vars %d funcs %d\n", gm_nobjs, gm_nvars, gm_nfuncs);
    for (int k = 2; k < argc; k++) {
        if (!strncmp(argv[k], "room=", 5)) { gm_goto_room(atoi(argv[k] + 5)); continue; }
        if (!strncmp(argv[k], "roomname=", 9)) { for (int r = 0; r < gm_nrooms; r++) if (!strcmp(gm_rooms[r].name, argv[k] + 9)) gm_goto_room(r); continue; }
        if (!strncmp(argv[k], "step=", 5)) { for (int q = atoi(argv[k] + 5); q > 0; q--) { gstep(); trace_vars(); } continue; }
        if (!strncmp(argv[k], "tap=", 4)) { tapvk = atoi(argv[k] + 4); for (tapph = 1; tapph <= 3; tapph++) { gstep(); trace_vars(); } tapvk = tapph = 0; continue; }
        if (!strncmp(argv[k], "press=", 6)) { if (npvk < 8) pvk[npvk++] = atoi(argv[k] + 6); continue; }
        if (!strncmp(argv[k], "hold=", 5)) { if (nhvk < 8) hvk[nhvk++] = atoi(argv[k] + 5); continue; }
        if (!strncmp(argv[k], "tr=", 3)) { if (ntr < 4) trv[ntr++] = argv[k] + 3; continue; }
        if (!strncmp(argv[k], "clrkeys", 7)) { npvk = nhvk = 0; continue; }
        if (!strncmp(argv[k], "period=", 7)) { pper = atoi(argv[k] + 7); continue; }
#ifdef GFX
        if (!strncmp(argv[k], "pack=", 5)) continue;
        if (!strncmp(argv[k], "snap=", 5)) { gm_draw(); ge_gfx_host_dump(argv[k] + 5); continue; }   /* snap=<file.ppm>: draw and dump now (between step= commands) */
        if (!strncmp(argv[k], "shot=", 5) && nshot < 16) { shotf[nshot] = atoi(argv[k] + 5); const char *c = strchr(argv[k] + 5, ':'); shotp[nshot++] = c ? c + 1 : "shot.ppm"; continue; }
#endif
        if (!strncmp(argv[k], "run=", 4)) { for (int q = 0; q < gm_nscripts; q++) if (gm_scripts[q].fn && !strcmp(gm_scripts[q].name, argv[k] + 4)) { int ot = gm_find_object("obj_time"); gm_inst *t = gm_obj1(ot); if (!t) t = gm_instance_create(0, 0, ot); gm_scripts[q].fn(t, t, 0, NULL); printf("ran %s\n", argv[k] + 4); } continue; }
        if (!strncmp(argv[k], "set=", 4)) { char nm[64]; double v; if (sscanf(argv[k] + 4, "%63[^:]:%lf", nm, &v) == 2) { int id = gm_vid(nm); if (id >= 0) gm_set(gm_global_inst, id, gm_real(v)); else printf("no var %s\n", nm); } continue; }
        if (!strncmp(argv[k], "msgend=", 7)) { for (int q = 1; q <= atoi(argv[k] + 7); q++) gm_seta(gm_global_inst, gm_vid("msg"), gm_real(q), gm_const_str(7)); continue; }   /* msgend=<n>: global.msg[1..n] = "%%%" (what earlier rooms leave behind; the writer loops need a terminator) */
        if (!strncmp(argv[k], "evu=", 4)) { char on[64]; int n; if (sscanf(argv[k] + 4, "%63[^:]:%d", on, &n) == 2) { gm_inst *m = gm_obj1(gm_find_object(on)); if (m) gm_run_event(m, EV_OTHER, EVO_USER0 + n); } continue; }   /* evu=<object>:<n>: run user event n on the first instance */
        if (!strncmp(argv[k], "mk=", 3)) { char on[64]; double tx, ty; if (sscanf(argv[k] + 3, "%63[^:]:%lf:%lf", on, &tx, &ty) == 3) gm_instance_create(tx, ty, gm_find_object(on)); continue; }   /* mk=<object>:x:y: create an instance */
        if (!strncmp(argv[k], "draw=", 5)) { drawstep = atoi(argv[k] + 5); continue; }
        if (!strncmp(argv[k], "tp=", 3)) { double tx, ty; if (sscanf(argv[k] + 3, "%lf:%lf", &tx, &ty) == 2) { gm_inst *m = gm_obj1(gm_find_object("obj_mainchara")); if (m) { m->x = tx; m->y = ty; } } continue; }
        if (!strncmp(argv[k], "mv=", 3)) { char on[64]; double tx, ty; if (sscanf(argv[k] + 3, "%63[^:]:%lf:%lf", on, &tx, &ty) == 3) { gm_inst *m = gm_obj1(gm_find_object(on)); if (m) { m->x = tx; m->y = ty; } } continue; }
        if (!strncmp(argv[k], "setv=", 5)) { char on[64], vn[64]; double v; if (sscanf(argv[k] + 5, "%63[^:]:%63[^:]:%lf", on, vn, &v) == 3) { gm_inst *m = gm_obj1(gm_find_object(on)); int id = gm_vid(vn); if (m && id >= 0) gm_set(m, id, gm_real(v)); } continue; }
        if (!strncmp(argv[k], "g=", 2)) { if (ngv < 16) gvars[ngv++] = argv[k] + 2; continue; }
        if (!strncmp(argv[k], "dump=", 5)) { dumpobj = argv[k] + 5; continue; }
        if (!strncmp(argv[k], "save=", 5)) { snprintf(gm_save_dir, sizeof gm_save_dir, "%s", argv[k] + 5); continue; }
        int o = gm_find_object(argv[k]);
        if (o < 0) { printf("no object %s\n", argv[k]); continue; }
        gm_inst *i = gm_instance_create(0, 0, o);
        printf("created %s id %d\n", argv[k], i ? i->id : -1);
    }
    if (gm_room_next >= 0) { gm_step(); }
    int last = gm_room_cur;
    for (int f = 0; f < frames; f++) {
        { struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); gm_step(); clock_gettime(CLOCK_MONOTONIC, &t1);
          long us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_nsec - t0.tv_nsec) / 1000;
          if (getenv("GT_STEPT") && us > atol(getenv("GT_STEPT"))) printf("frame %d room %d: step %ld us\n", gm_frame, gm_room_cur, us); }
        gm_draw(); trace_vars();
#ifdef GFX
        for (int q = 0; q < nshot; q++) if (shotf[q] == f) ge_gfx_host_dump(shotp[q]);
#endif
        if (gm_room_cur != last) { printf("frame %d: room %d (%s), %d instances\n", f, gm_room_cur, gm_room_cur >= 0 && gm_room_cur < gm_nrooms ? gm_rooms[gm_room_cur].name : "?", gm_instance_count(-1)); last = gm_room_cur; }
    }
    for (int k = 0, n = gm_instance_count(-1); k < n && k < 12; k++) { gm_inst *i = gm_instance_nth(-1, k); printf("  inst %d %s (%.0f,%.0f)\n", i->id, gm_objs[i->obj].name, i->x, i->y); }
    if (dumpobj) for (int k = 0, n = gm_instance_count(-1); k < n; k++) {
        gm_inst *i = gm_instance_nth(-1, k);
        if (strcmp(gm_objs[i->obj].name, dumpobj)) continue;
        printf("--- %s id %d at (%.0f,%.0f) depth %.0f spr %d alarm0 %d alarm1 %d\n", dumpobj, i->id, i->x, i->y, (double)i->depth, i->sprite, i->alarm[0], i->alarm[1]);
        for (int c = 0; c < i->vars.cap; c++) if (i->vars.key[c]) {
            gm_val v = i->vars.val[c]; char t[64];
            printf("  %s = %s\n", gm_var_names[i->vars.key[c] - 1], v.t == GM_ARR ? "[array]" : gm_cstr(v, t, sizeof t));
        }
    }
    if (getenv("GT_LIST")) for (int k = 0, n = gm_instance_count(-1); k < n; k++) { gm_inst *i = gm_instance_nth(-1, k); const char *nm = gm_objs[i->obj].name; if (getenv("GT_ALL") || (!strstr(nm, "solid") && !strstr(nm, "door"))) printf("  * %s (%.0f,%.0f) vis %d spr %d\n", nm, i->x, i->y, i->visible, i->sprite); }
    { int cx, cy; int vv = gm_camera(&cx, &cy); printf("camera %d: %d,%d\n", vv, cx, cy); }
    for (int g = 0; g < ngv; g++) { char t[64]; int v = gm_vid(gvars[g]); gm_val x = v >= 0 ? gm_get(gm_global_inst, v) : GM_UNDEF_V; printf("global.%s = %s\n", gvars[g], x.t == GM_UNDEF ? "undefined" : gm_cstr(x, t, sizeof t)); }
    for (int o = 0; o < gm_nobjs; o++) { int c = gm_instance_count(o); if (c > 50) printf("  %d x %s\n", c, gm_objs[o].name); }
    printf("done: frame %llu instances %d room %d\n", (unsigned long long)gm_frame, gm_instance_count(-1), gm_room_cur);
    return 0;
}
