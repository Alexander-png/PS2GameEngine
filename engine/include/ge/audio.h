/* ge/audio.h - platform audio backend used by the GML audio layer (engine/core/gml_audio.c).
 * snd is the GML sound asset id (index into gm_sounds[]); gain is 0..1 (asset volume already applied), pitch is a ratio. */
#ifndef GE_AUDIO_H
#define GE_AUDIO_H
typedef struct ge_audio_be {
    int  (*start)(int snd, int loop, float gain, float pitch);   /* backend handle >= 0, or -1 when it cannot play */
    void (*set)(int h, float gain, float pitch);
    void (*pause)(int h, int on);
    void (*stop)(int h);
    int  (*playing)(int h);                                      /* 1 while the sound is still audible/paused (not finished or stolen) */
    void (*tick)(void);                                          /* once per game step */
} ge_audio_be;
extern const ge_audio_be *ge_audio;                              /* NULL = no audio hardware: timing is simulated from the sound lengths */
#endif
