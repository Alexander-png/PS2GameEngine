/* gml_audio.c - GML audio builtins (audio_play_sound & co). Sounds are assets (gm_sounds[]); every play returns an instance
 * handle (>= ID0) that audio_stop_sound / audio_sound_gain / ... accept, or a plain asset id which then applies to all instances. */
#include <string.h>
#include "gml/gml_rt.h"
#include "gml/gm_sounds.h"
#include "ge/audio.h"

const ge_audio_be *ge_audio;
#define R(x) gm_real(x)
#define NH 48
#define ID0 100000

typedef struct { int id, snd, bh, loop, paused, age, fade_left; float gain, pitch, fade_step; } ah_t;
static ah_t H[NH];
static int seq = 1, aframe;

static int valid_snd(int s) { return s >= 0 && s < gm_nsounds && gm_sounds[s].kind >= 0; }
static ah_t *find(int id)
{
    if (id < ID0) return NULL;
    ah_t *h = &H[(id - ID0) % NH];
    return h->id == id ? h : NULL;
}
static void push(ah_t *h)
{
    if (ge_audio && h->bh >= 0) ge_audio->set(h->bh, h->gain * gm_sounds[h->snd].volume, h->pitch);
}
static void kill(ah_t *h)
{
    if (ge_audio && h->bh >= 0) ge_audio->stop(h->bh);
    h->id = 0;
}
/* apply f to the instance `id` or, for a plain asset id, to all its instances */
#define EACH(id, h) for (int _i = 0; _i < NH; _i++) for (ah_t *h = &H[_i]; h && h->id && ((id) >= ID0 ? h->id == (id) : h->snd == (id)); h = NULL)

GMB(audio_play_sound)
{
    (void)self; (void)other;
    int snd = A_INT(0), loop = argc > 2 && A_REAL(2) != 0;
    if (!valid_snd(snd)) return R(-1);
    ah_t *h = NULL;
    for (int i = 0; i < NH && !h; i++) if (!H[i].id) h = &H[i];
    if (!h) { h = &H[0]; for (int i = 1; i < NH; i++) if (H[i].age < h->age) h = &H[i]; kill(h); }
    int slot = (int)(h - H);
    memset(h, 0, sizeof *h);
    h->id = ID0 + seq * NH + slot; seq = (seq + 1) & 0xFFFFF;
    h->snd = snd; h->loop = loop; h->gain = 1; h->pitch = 1; h->age = aframe; h->bh = -1;
    if (ge_audio) h->bh = ge_audio->start(snd, loop, gm_sounds[snd].volume, 1.0f);
    return R(h->id);
}
GMB(audio_stop_sound) { (void)self; (void)other; if (!argc) return GM_UNDEF_V; int id = A_INT(0); EACH(id, h) kill(h); return GM_UNDEF_V; }
GMB(audio_stop_all) { (void)self; (void)other; (void)argc; (void)argv; for (int i = 0; i < NH; i++) if (H[i].id) kill(&H[i]); return GM_UNDEF_V; }
GMB(audio_pause_sound) { (void)self; (void)other; if (!argc) return GM_UNDEF_V; int id = A_INT(0); EACH(id, h) { h->paused = 1; if (ge_audio && h->bh >= 0) ge_audio->pause(h->bh, 1); } return GM_UNDEF_V; }
GMB(audio_resume_sound) { (void)self; (void)other; if (!argc) return GM_UNDEF_V; int id = A_INT(0); EACH(id, h) { h->paused = 0; if (ge_audio && h->bh >= 0) ge_audio->pause(h->bh, 0); } return GM_UNDEF_V; }
GMB(audio_is_playing) { (void)self; (void)other; if (!argc) return R(0); int id = A_INT(0); int n = 0; EACH(id, h) n++; return R(n > 0); }
GMB(audio_sound_pitch)
{
    (void)self; (void)other;
    if (argc < 2) return GM_UNDEF_V;
    int id = A_INT(0); float p = (float)A_REAL(1);
    if (p < 0.05f) p = 0.05f;
    EACH(id, h) { h->pitch = p; push(h); }
    return GM_UNDEF_V;
}
GMB(audio_sound_gain)
{
    (void)self; (void)other;
    if (argc < 2) return GM_UNDEF_V;
    int id = A_INT(0); float g = (float)A_REAL(1); int ms = argc > 2 ? (int)A_REAL(2) : 0;
    if (g < 0) g = 0;
    EACH(id, h) {
        if (ms > 0) {
            int fr = (int)(ms * (gm_room_speed > 0 ? gm_room_speed : 30) / 1000); if (fr < 1) fr = 1;
            h->fade_left = fr; h->fade_step = (g - h->gain) / fr;
        } else { h->gain = g; h->fade_left = 0; push(h); }
    }
    return GM_UNDEF_V;
}
GMB(audio_sound_get_gain) { (void)self; (void)other; if (!argc) return R(1); int id = A_INT(0); EACH(id, h) return R(h->gain); return R(1); }
GMB(audio_sound_get_pitch) { (void)self; (void)other; if (!argc) return R(1); int id = A_INT(0); EACH(id, h) return R(h->pitch); return R(1); }
GMB(audio_sound_get_track_position)
{
    (void)self; (void)other; if (!argc) return R(0);
    int id = A_INT(0);
    EACH(id, h) return R((aframe - h->age) / (gm_room_speed > 0 ? gm_room_speed : 30));
    return R(0);
}
GMB(audio_sound_set_track_position) { (void)self; (void)other; (void)argc; (void)argv; return GM_UNDEF_V; }
GMB(audio_channel_num) { (void)self; (void)other; (void)argc; (void)argv; return R(NH); }

/* once per game step: gain fades, drop finished sounds */
void gm_audio_tick(void)
{
    aframe++;
    float spd = gm_room_speed > 0 ? gm_room_speed : 30;
    for (int i = 0; i < NH; i++) {
        ah_t *h = &H[i];
        if (!h->id) continue;
        if (h->fade_left > 0) { h->gain += h->fade_step; if (--h->fade_left == 0 && h->gain < 0) h->gain = 0; push(h); }
        if (h->paused) { h->age++; continue; }
        int alive;
        if (ge_audio && h->bh >= 0) alive = ge_audio->playing(h->bh);
        else alive = h->loop || (aframe - h->age) * 1000.0f / spd < gm_sounds[h->snd].dur_ms;
        if (!alive) h->id = 0;
    }
    if (ge_audio && ge_audio->tick) ge_audio->tick();
}
