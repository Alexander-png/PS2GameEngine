/* gml_lib.c - GML builtin functions (gmb_<name>). A strong definition here overrides the converter's weak "unimplemented" stub.
 * Platform-facing ones (draw, audio, input, files) are first silent no-ops / hook calls; the platform layer replaces them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "gml/gml_rt.h"

gm_inst **gm_inst_list(int *n);
#define gm_inst_list_pub gm_inst_list
#define GMBW(name) __attribute__((weak)) GMB(name)
#define R(x) gm_real(x)
#define PI 3.14159265358979323846

/* ---------------- math ---------------- */
GMB(abs) { (void)self; (void)other; return R(fabs(A_REAL(0))); }
GMB(sin) { (void)self; (void)other; return R(sin(A_REAL(0))); }
GMB(cos) { (void)self; (void)other; return R(cos(A_REAL(0))); }
GMB(floor) { (void)self; (void)other; return R(floor(A_REAL(0))); }
GMB(ceil) { (void)self; (void)other; return R(ceil(A_REAL(0))); }
GMB(round) { (void)self; (void)other; return R(rint(A_REAL(0))); }
GMB(power) { (void)self; (void)other; return R(pow(A_REAL(0), A_REAL(1))); }
GMB(degtorad) { (void)self; (void)other; return R(A_REAL(0) * PI / 180.0); }
GMB(min)
{
    (void)self; (void)other;
    gm_real_t m = A_REAL(0);
    for (int k = 1; k < argc; k++) { gm_real_t v = A_REAL(k); if (v < m) m = v; }
    return R(m);
}
GMB(lengthdir_x) { (void)self; (void)other; return R(A_REAL(0) * cos(A_REAL(1) * PI / 180.0)); }
GMB(lengthdir_y) { (void)self; (void)other; return R(-A_REAL(0) * sin(A_REAL(1) * PI / 180.0)); }
GMB(point_distance) { (void)self; (void)other; gm_real_t dx = A_REAL(2) - A_REAL(0), dy = A_REAL(3) - A_REAL(1); return R(sqrt(dx * dx + dy * dy)); }
GMB(point_direction)
{
    (void)self; (void)other;
    gm_real_t dx = A_REAL(2) - A_REAL(0), dy = A_REAL(3) - A_REAL(1);
    if (dx == 0 && dy == 0) return R(0);
    gm_real_t d = atan2(-dy, dx) * 180.0 / PI;
    return R(d < 0 ? d + 360 : d);
}
GMB(distance_to_point) { (void)other; gm_real_t dx = A_REAL(0) - self->x, dy = A_REAL(1) - self->y; return R(sqrt(dx * dx + dy * dy)); }
GMB(distance_to_object)
{
    (void)other;
    gm_inst *t = gm_obj1(A_INT(0));
    if (!t) return R(1000000);
    gm_real_t dx = t->x - self->x, dy = t->y - self->y;
    return R(sqrt(dx * dx + dy * dy));
}
GMB(merge_color)
{
    (void)self; (void)other;
    int a = A_INT(0), b = A_INT(1);
    gm_real_t t = A_REAL(2);
    int out = 0;
    for (int s = 0; s < 24; s += 8) {
        int ca = (a >> s) & 255, cb = (b >> s) & 255;
        out |= ((int)(ca + (cb - ca) * t) & 255) << s;
    }
    return R(out);
}
GMB(make_color_rgb) { (void)self; (void)other; return R((A_INT(0) & 255) | ((A_INT(1) & 255) << 8) | ((A_INT(2) & 255) << 16)); }
GMB(make_color_hsv)
{
    (void)self; (void)other;
    gm_real_t h = A_REAL(0) / 255.0 * 6.0, s = A_REAL(1) / 255.0, v = A_REAL(2) / 255.0;
    int i = (int)floor(h) % 6;
    gm_real_t f = h - floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f)), r, g, b;
    switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                 case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; break; }
    return R((int)(r * 255) | ((int)(g * 255) << 8) | ((int)(b * 255) << 16));
}

/* ---------------- random (xorshift32, seedable) ---------------- */
static uint32_t rng = 0x2545F491u;
static uint32_t rnd32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static gm_real_t rnd01(void) { return (rnd32() >> 8) / 16777216.0; }
GMB(random) { (void)self; (void)other; return R(rnd01() * A_REAL(0)); }
GMB(randomize) { (void)self; (void)other; (void)argc; (void)argv; rng = (uint32_t)time(NULL) | 1; return GM_UNDEF_V; }
GMB(choose)
{
    (void)self; (void)other;
    if (argc <= 0) return GM_UNDEF_V;
    return argv[(int)(rnd01() * argc) % argc];
}

/* ---------------- strings ---------------- */
GMB(string)
{
    (void)self; (void)other;
    return gm_conv_s(GM_ARG(0));
}
GMB(real) { (void)self; (void)other; return R(A_REAL(0)); }
/* GameMaker strings are sequences of characters: the converter emits UTF-8, so indices are code points */
static int u8_len(const char *s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n; }
static const char *u8_at(const char *s, int idx)       /* byte pointer of character idx (0-based); end of string when out of range */
{
    while (*s && idx > 0) { s++; while (((unsigned char)*s & 0xC0) == 0x80) s++; idx--; }
    return s;
}
static int u8_enc(unsigned c, char *o)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
    o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3;
}
GMB(string_length) { (void)self; (void)other; char t[64]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t); return R((gm_real_t)u8_len(s)); }
GMB(string_copy)
{
    (void)self; (void)other;
    char t[64]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t);
    int i = A_INT(1), c = A_INT(2), n = u8_len(s);
    if (i < 1) i = 1;
    if (c < 1 || i > n) return gm_str_make("", 0);
    if (i - 1 + c > n) c = n - (i - 1);
    const char *b = u8_at(s, i - 1), *e = u8_at(b, c);
    return gm_str_make(b, (int)(e - b));
}
GMB(string_insert)
{
    (void)self; (void)other;
    char t1[64], t2[64];
    const char *sub = gm_cstr(GM_ARG(0), t1, sizeof t1), *s = gm_cstr(GM_ARG(1), t2, sizeof t2);
    int i = A_INT(2), n = u8_len(s);
    if (i < 1) i = 1;
    if (i > n + 1) i = n + 1;
    const char *p = u8_at(s, i - 1);
    int a = (int)(p - s), sl = (int)strlen(sub), tl = (int)strlen(s);
    char *buf = malloc(tl + sl + 1);
    memcpy(buf, s, a); memcpy(buf + a, sub, sl); memcpy(buf + a + sl, p, tl - a);
    gm_val r = gm_str_make(buf, tl + sl);
    free(buf);
    return r;
}
GMB(string_upper)
{
    (void)self; (void)other;
    char t[64]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t);
    int n = (int)strlen(s);
    gm_str *o = gm_str_new(s, n);
    char *w = (char *)o->s;
    for (int k = 0; k < n; k++) w[k] = (char)toupper((unsigned char)w[k]);
    return gm_str_val(o);
}
GMB(string_lower)
{
    (void)self; (void)other;
    char t[64]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t);
    int n = (int)strlen(s);
    gm_str *o = gm_str_new(s, n);
    char *w = (char *)o->s;
    for (int k = 0; k < n; k++) w[k] = (char)tolower((unsigned char)w[k]);
    return gm_str_val(o);
}
GMB(chr) { (void)self; (void)other; char c[4]; int n = u8_enc((unsigned)A_INT(0), c); return gm_str_make(c, n); }
GMB(ord)
{
    (void)self; (void)other;
    char t[64]; const unsigned char *s = (const unsigned char *)gm_cstr(GM_ARG(0), t, sizeof t);
    unsigned c = s[0];
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) return R(((c & 0x1F) << 6) | (s[1] & 0x3F));
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) return R(((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
    return R(c);
}
GMB(string_char_at)
{
    (void)self; (void)other;
    char t[64]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t);
    int i = A_INT(1);
    if (i < 1 || i > u8_len(s)) return gm_str_make("", 0);
    const char *b = u8_at(s, i - 1), *e = u8_at(b, 1);
    return gm_str_make(b, (int)(e - b));
}
GMB(string_delete)
{
    (void)self; (void)other;
    char t[64]; const char *s = gm_cstr(GM_ARG(0), t, sizeof t);
    int n = u8_len(s), p = A_INT(1), c = A_INT(2), tl = (int)strlen(s);
    if (p < 1 || p > n || c < 1) return gm_str_make(s, tl);
    if (p - 1 + c > n) c = n - (p - 1);
    const char *b = u8_at(s, p - 1), *e = u8_at(b, c);
    int a = (int)(b - s), z = (int)(e - s);
    char *buf = malloc(tl + 1);
    memcpy(buf, s, a); memcpy(buf + a, s + z, tl - z);
    gm_val r = gm_str_make(buf, tl - (z - a));
    free(buf);
    return r;
}
static int u8_len_n(const char *s, int bytes) { int n = 0; for (int k = 0; k < bytes; k++) if (((unsigned char)s[k] & 0xC0) != 0x80) n++; return n; }
GMB(string_pos)
{
    (void)self; (void)other;
    char t1[64], t2[64];
    const char *sub = gm_cstr(GM_ARG(0), t1, sizeof t1), *s = gm_cstr(GM_ARG(1), t2, sizeof t2);
    const char *p = strstr(s, sub);
    return R(p ? (gm_real_t)(u8_len_n(s, (int)(p - s)) + 1) : 0);
}
GMBW(string_width) { (void)self; (void)other; char t[64]; return R(8.0 * strlen(gm_cstr(GM_ARG(0), t, sizeof t))); }
GMBW(string_height) { (void)self; (void)other; (void)argc; (void)argv; return R(16); }

/* ---------------- instances / objects / rooms ---------------- */
GMB(instance_create)
{
    (void)self; (void)other;
    gm_inst *i = gm_instance_create(A_REAL(0), A_REAL(1), A_INT(2));
    return R(i ? i->id : -4);
}
GMB(instance_destroy) { (void)other; (void)argc; (void)argv; gm_instance_destroy(self); return GM_UNDEF_V; }
GMB(instance_exists)
{
    (void)self; (void)other;
    gm_val v = GM_ARG(0);
    int id = gm_to_int(v);
    if (id >= 100000) return R(gm_instance_by_id(id) != NULL);
    if (id == -3) return R(gm_instance_count(-1) > 0);
    if (id == -1) return R(self != NULL);
    if (id == -2) return R(other != NULL);
    if (id < 0) return R(0);
    return R(gm_instance_count(id) > 0);
}
GMB(instance_number) { (void)self; (void)other; return R(gm_instance_count(A_INT(0))); }
GMB(instance_find)
{
    (void)self; (void)other;
    gm_inst *i = gm_instance_nth(A_INT(0), A_INT(1));
    return R(i ? i->id : -4);
}
GMB(instance_change)
{
    (void)other;
    int o = A_INT(0);
    if (o >= 0 && o < gm_nobjs) {
        { extern unsigned inst_epoch; inst_epoch++; } self->obj = o;
        if (A_INT(1)) gm_run_event(self, EV_CREATE, 0);
    }
    return GM_UNDEF_V;
}
GMB(instance_deactivate_all) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(event_inherited) { (void)other; (void)argc; (void)argv; gm_event_inherited(self); return GM_UNDEF_V; }
GMB(event_user) { (void)other; gm_run_event(self, EV_OTHER, EVO_USER0 + A_INT(0)); return GM_UNDEF_V; }
GMB(event_perform) { (void)other; gm_run_event(self, A_INT(0), A_INT(1)); return GM_UNDEF_V; }
GMB(script_execute)
{
    int s = A_INT(0);
    if (s < 0 || s >= gm_nscripts || !gm_scripts[s].fn) return GM_UNDEF_V;
    return gm_scripts[s].fn(self, other, argc > 0 ? argc - 1 : 0, argv + 1);
}
GMB(room_goto) { (void)self; (void)other; gm_logf("room_goto %d (cur %d, currentroom %d)", A_INT(0), gm_room_cur, (int)gm_to_int(gm_get(GM_GLOBAL, 136))); gm_goto_room(A_INT(0)); return GM_UNDEF_V; }
GMB(room_goto_next) { (void)self; (void)other; (void)argc; (void)argv; gm_goto_room(gm_room_cur + 1); return GM_UNDEF_V; }
GMB(room_goto_previous) { (void)self; (void)other; (void)argc; (void)argv; gm_goto_room(gm_room_cur - 1); return GM_UNDEF_V; }
GMB(room_next) { (void)self; (void)other; return R(A_INT(0) + 1); }
GMB(room_previous) { (void)self; (void)other; return R(A_INT(0) - 1); }
GMB(room_restart) { (void)self; (void)other; (void)argc; (void)argv; gm_goto_room(gm_room_cur); return GM_UNDEF_V; }
GMB(room_set_persistent) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(game_end) { (void)self; (void)other; (void)argc; (void)argv; gm_logf("game_end"); return GM_UNDEF_V; }
GMB(game_restart) { (void)self; (void)other; (void)argc; (void)argv; gm_logf("game_restart"); return GM_UNDEF_V; }

/* ---------------- input (via hook) ---------------- */
static unsigned keys(int vk) { return gm_key_state(vk); }
GMB(keyboard_check) { (void)self; (void)other; return R(keys(A_INT(0)) & 1); }
GMB(keyboard_check_direct) { (void)self; (void)other; return R(keys(A_INT(0)) & 1); }
GMB(keyboard_check_pressed) { (void)self; (void)other; return R((keys(A_INT(0)) >> 1) & 1); }
GMB(keyboard_check_released) { (void)self; (void)other; return R((keys(A_INT(0)) >> 2) & 1); }
GMB(keyboard_clear) { (void)self; (void)other; (void)argc; gm_key_clear(A_INT(0)); return GM_UNDEF_V; }
GMB(keyboard_key_press) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(keyboard_key_release) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(keyboard_set_map) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(joystick_exists) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }
GMB(joystick_has_pov) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }

/* ---------------- steam / window / misc: silent ---------------- */
GMB(steam_initialised) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }
GMB(steam_file_exists) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }
GMB(window_set_caption) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(window_set_fullscreen) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(window_get_fullscreen) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }
GMB(window_center) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(window_get_x) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }
GMB(window_get_y) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }
GMB(window_set_position) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(date_current_datetime) { (void)self; (void)other; (void)argc; (void)argv; return R(40000.0 + (gm_real_t)(time(NULL) % 86400) / 86400.0); }

/* ---------------- draw: silent until the renderer is attached ---------------- */
#define NOPF(n) GMBW(n) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
NOPF(draw_set_valign) NOPF(draw_self) NOPF(draw_text_color) NOPF(draw_set_color) NOPF(draw_set_font) NOPF(draw_set_alpha) NOPF(draw_set_halign) NOPF(draw_set_circle_precision)
NOPF(draw_text) NOPF(draw_text_ext) NOPF(draw_text_transformed) NOPF(draw_text_transformed_color)
NOPF(draw_sprite) NOPF(draw_sprite_ext) NOPF(draw_sprite_part) NOPF(draw_sprite_part_ext) NOPF(draw_sprite_stretched)
NOPF(draw_background) NOPF(draw_background_part_ext)
NOPF(draw_rectangle) NOPF(draw_rectangle_color) NOPF(draw_line) NOPF(draw_line_width) NOPF(draw_line_width_color) NOPF(draw_line_color)
NOPF(draw_circle) NOPF(draw_triangle) NOPF(draw_triangle_color) NOPF(draw_point_color) NOPF(draw_roundrect) NOPF(draw_ellipse_color)
NOPF(tile_layer_hide) NOPF(tile_layer_show) NOPF(tile_layer_shift)
GMBW(draw_getpixel) { (void)self; (void)other; (void)argc; (void)argv; return R(0); }

/* audio builtins live in gml_audio.c */

/* ---------------- sprites: sizes come later from the asset tables ---------------- */

/* ---------------- D&D actions ---------------- */
static int act_rel;
GMB(action_set_relative) { (void)self; (void)other; act_rel = A_INT(0); return GM_UNDEF_V; }
static int take_rel(void) { int r = act_rel; act_rel = 0; return r; }
GMB(action_move_to) { (void)other; int r = take_rel(); self->x = r ? self->x + A_REAL(0) : A_REAL(0); self->y = r ? self->y + A_REAL(1) : A_REAL(1); return GM_UNDEF_V; }
GMB(action_kill_object) { (void)other; (void)argc; (void)argv; gm_instance_destroy(self); return GM_UNDEF_V; }
GMB(action_set_alarm) { (void)other; int r = take_rel(), a = A_INT(1); if (a >= 0 && a < GM_MAX_ALARMS) self->alarm[a] = (int)(r && self->alarm[a] > 0 ? self->alarm[a] + A_REAL(0) : A_REAL(0)); return GM_UNDEF_V; }
static void polar(gm_inst *i, gm_real_t sp, gm_real_t dir) { i->direction = dir; i->hspeed = sp * cos(dir * PI / 180.0); i->vspeed = -sp * sin(dir * PI / 180.0); }
GMB(action_set_motion) { (void)other; int r = take_rel(); gm_real_t sp = A_REAL(1), dir = A_REAL(0); (void)r; polar(self, sp, dir); return GM_UNDEF_V; }
GMB(action_set_hspeed) { (void)other; int r = take_rel(); self->hspeed = r ? self->hspeed + A_REAL(0) : A_REAL(0); return GM_UNDEF_V; }
GMB(action_set_gravity) { (void)other; int r = take_rel(); self->gravity_dir = r ? self->gravity_dir + A_REAL(0) : A_REAL(0); self->gravity = r ? self->gravity + A_REAL(1) : A_REAL(1); return GM_UNDEF_V; }
GMB(action_set_friction) { (void)other; int r = take_rel(); self->friction = r ? self->friction + A_REAL(0) : A_REAL(0); return GM_UNDEF_V; }
GMB(action_move)      /* D&D move: 9 flags laid out like a numpad (1..9), one of the set directions is picked at random; the middle one stops */
{
    (void)other; (void)argc;
    char t[96]; const char *d = gm_cstr(GM_ARG(0), t, sizeof t);
    int r = take_rel(), cand[9], n = 0;
    for (int k = 0; k < 9 && d[k]; k++) if (d[k] == '1') cand[n++] = k;
    if (!n) return GM_UNDEF_V;
    int c = cand[n > 1 ? (int)(rnd01() * n) % n : 0];
    static const int ang[9] = {225, 270, 315, 180, 0, 0, 135, 90, 45};
    gm_real_t sp = A_REAL(1);
    if (c == 4) { polar(self, 0, self->direction); return GM_UNDEF_V; }
    if (r) { gm_real_t cur = sqrt(self->hspeed * self->hspeed + self->vspeed * self->vspeed); sp += cur; }
    polar(self, sp, ang[c]);
    return GM_UNDEF_V;
}
GMB(action_move_point) { (void)other; take_rel(); gm_real_t dx = A_REAL(0) - self->x, dy = A_REAL(1) - self->y, d = sqrt(dx * dx + dy * dy); gm_real_t sp = A_REAL(2); if (d > 0) polar(self, sp, atan2(-dy, dx) * 180.0 / PI); return GM_UNDEF_V; }
GMB(action_create_object) { (void)other; int r = take_rel(); gm_inst *i = gm_instance_create(r ? self->x + A_REAL(1) : A_REAL(1), r ? self->y + A_REAL(2) : A_REAL(2), A_INT(0)); return gm_real(i ? i->id : -4); }
GMB(action_previous_room) { (void)self; (void)other; (void)argc; (void)argv; gm_goto_room(gm_room_cur - 1); return GM_UNDEF_V; }
GMB(move_towards_point) { (void)other; gm_real_t dx = A_REAL(0) - self->x, dy = A_REAL(1) - self->y; if (dx || dy) polar(self, A_REAL(2), atan2(-dy, dx) * 180.0 / PI); return GM_UNDEF_V; }
GMB(move_snap) { (void)other; gm_real_t h = A_REAL(0), v = A_REAL(1); if (h > 0) self->x = floor(self->x / h + 0.5) * h; if (v > 0) self->y = floor(self->y / v + 0.5) * v; return GM_UNDEF_V; }
GMB(path_start) { (void)other; (void)argc; gm_path_start(self, A_INT(0), A_REAL(1), A_INT(2), A_INT(3)); return GM_UNDEF_V; }
GMB(path_end) { (void)other; (void)argc; (void)argv; gm_path_stop(self); return GM_UNDEF_V; }
NOPF(path_dummy_) NOPF(sprite_delete) NOPF(sprite_replace) NOPF(sprite_create_from_surface)
GMB(background_add) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(-1); }
GMB(sprite_get_name) { (void)self; (void)other; (void)argc; (void)argv; return gm_str_make("", 0); }
GMB(sprite_collision_mask) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
/* collisions: bounding boxes arrive with the sprite tables; for now nothing collides */
GMB(joystick_check_button) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(0); }
GMB(joystick_buttons) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(0); }
GMB(joystick_xpos) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(0); }
GMB(joystick_ypos) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(0); }
GMB(joystick_direction) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(0); }
GMB(joystick_pov) { (void)self; (void)other; (void)argc; (void)argv; return gm_real(-1); }

/* ---------------- sprites / collisions ---------------- */
static int spr(int k) { return k >= 0 && k < gm_nsprites; }
GMB(sprite_get_width) { (void)self; (void)other; int s = A_INT(0); return gm_real(spr(s) ? gm_sprites[s].w : 0); }
GMB(sprite_get_height) { (void)self; (void)other; int s = A_INT(0); return gm_real(spr(s) ? gm_sprites[s].h : 0); }
GMB(sprite_get_xoffset) { (void)self; (void)other; int s = A_INT(0); return gm_real(spr(s) ? gm_sprites[s].ox : 0); }
GMB(sprite_get_yoffset) { (void)self; (void)other; int s = A_INT(0); return gm_real(spr(s) ? gm_sprites[s].oy : 0); }
GMB(sprite_get_number) { (void)self; (void)other; int s = A_INT(0); return gm_real(spr(s) ? gm_sprites[s].nframes : 0); }
GMB(sprite_exists) { (void)self; (void)other; return gm_real(spr(A_INT(0))); }
static gm_val idof(gm_inst *i) { return gm_real(i ? i->id : -4); }
GMB(collision_point)
{ (void)other; gm_real_t x = A_REAL(0), y = A_REAL(1); return idof(gm_collide_rect(self, x, y, x, y, A_INT(2), A_INT(4))); }
GMB(instance_position)
{ (void)self; (void)other; gm_real_t x = A_REAL(0), y = A_REAL(1); return idof(gm_collide_rect(NULL, x, y, x, y, A_INT(2), 0)); }
GMB(position_meeting)
{ (void)other; gm_real_t x = A_REAL(0), y = A_REAL(1); return gm_real(gm_collide_rect(self, x, y, x, y, A_INT(2), 1) != NULL); }
GMB(collision_rectangle)
{
    (void)other;
    gm_real_t x1 = A_REAL(0), y1 = A_REAL(1), x2 = A_REAL(2), y2 = A_REAL(3);
    return idof(gm_collide_rect(self, x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2, x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1, A_INT(4), A_INT(6)));
}
GMB(collision_circle)
{
    (void)other;
    gm_real_t x = A_REAL(0), y = A_REAL(1), r = A_REAL(2);
    extern unsigned (*gm_prof_cyc)(void); extern void gm_prof_fn(int, unsigned);
    unsigned c0 = gm_prof_cyc ? gm_prof_cyc() : 0;
    gm_val res = idof(gm_collide_rect(self, x - r, y - r, x + r, y + r, A_INT(3), A_INT(5)));
    if (gm_prof_cyc) gm_prof_fn(1, gm_prof_cyc() - c0);
    return res;
}
GMB(collision_line)
{
    (void)other;
    gm_real_t x1 = A_REAL(0), y1 = A_REAL(1), x2 = A_REAL(2), y2 = A_REAL(3);
    int who = A_INT(4), notme = A_INT(6), n;
    gm_inst **L = gm_inst_list_pub(&n);
    for (int k = 0; k < n; k++) {
        gm_inst *o = L[k];
        gm_real_t l, t, r, b;
        if (o->dead || (notme && o == self) || !gm_inst_bbox(o, &l, &t, &r, &b)) continue;
        if (who != -3 && !(who >= 100000 ? o->id == who : gm_obj_isa(o->obj, who))) continue;
        /* Liang-Barsky segment vs rectangle */
        gm_real_t u0 = 0, u1 = 1, dx = x2 - x1, dy = y2 - y1;
        gm_real_t p[4] = {-dx, dx, -dy, dy}, q[4] = {x1 - l, r - x1, y1 - t, b - y1};
        int hit = 1;
        for (int e = 0; e < 4 && hit; e++) {
            if (p[e] == 0) { if (q[e] < 0) hit = 0; }
            else { gm_real_t u = q[e] / p[e]; if (p[e] < 0) { if (u > u1) hit = 0; else if (u > u0) u0 = u; } else { if (u < u0) hit = 0; else if (u < u1) u1 = u; } }
        }
        if (hit) return idof(o);
    }
    return gm_real(-4);
}
GMB(place_meeting) { (void)other; return gm_real(gm_place(self, A_REAL(0), A_REAL(1), A_INT(2)) != NULL); }
GMB(instance_place) { (void)other; return idof(gm_place(self, A_REAL(0), A_REAL(1), A_INT(2))); }
GMB(place_free)
{
    (void)other;
    gm_real_t l, t, r, b;
    gm_inst tmp = *self;
    tmp.x = A_REAL(0); tmp.y = A_REAL(1);
    if (!gm_inst_bbox(&tmp, &l, &t, &r, &b)) return gm_real(1);
    int n; gm_inst **L = gm_inst_list_pub(&n);
    for (int k = 0; k < n; k++) {
        gm_inst *o = L[k]; gm_real_t ol, ot, orr, ob;
        if (o == self || o->dead || !o->solid || !gm_inst_bbox(o, &ol, &ot, &orr, &ob)) continue;
        if (l <= orr && ol <= r && t <= ob && ot <= b) return gm_real(0);
    }
    return gm_real(1);
}

void gm_lib_init(void)
{
    rng = 0x2545F491u;
}
