/* ge/room.h - platform independent room runtime (works on any converted GameMaker game). */
#ifndef GE_ROOM_H
#define GE_ROOM_H
#include <stdint.h>
#include "ge/gfx.h"

typedef struct ge_pack ge_pack_t;
/* v4 pack tables (GML runtime) */
typedef struct { uint32_t first; uint16_t nf, pad; float step; } ge_sprite_t;                 /* frames [first, first+nf) */
typedef struct { uint32_t asset; uint16_t w, h; int16_t rx, ry; } ge_frame_t;                 /* asset 0xFFFFFFFF = none; (rx,ry) = offset from the sprite origin */
typedef struct { uint32_t asset; uint16_t w, h; uint32_t pad; } ge_bg_t;
typedef struct { uint32_t asset; uint16_t w, h; uint32_t first_glyph; uint16_t nglyph, line_h, first_char, last_char, em, pad; } ge_font_t;
typedef struct { uint16_t ch, x, y, w, h; int16_t shift, offset; uint16_t pad; } ge_glyph_t;
/* a sprite instance placed in the room (state the runtime / GML layer reads and writes; matches GameMaker's instance variables) */
typedef struct { float x, y, xscale, yscale, angle; uint32_t color; float image_speed, image_index; int32_t sprite; uint32_t visible; } ge_inst_t;
typedef struct ge_room ge_room_t;

ge_pack_t *ge_pack_open(const char *name);                 /* .gmpk v3 (shared asset table, textures streamed on first draw) */
int        ge_pack_room_count(const ge_pack_t *p);
const char *ge_pack_room_name(const ge_pack_t *p, int i);
int        ge_pack_find(const ge_pack_t *p, const char *room);   /* index or -1 */

ge_room_t *ge_room_load_index(ge_pack_t *p, int i);        /* reads the room blob; textures stream in on demand */
void ge_room_free(ge_room_t *r);                           /* frees the room; its textures stay cached in VRAM until the space is needed */
void ge_room_prefetch(const ge_room_t *r, int cam_x, int cam_y);   /* upload textures visible from this camera; call outside a frame */
int  ge_room_speed(const ge_room_t *r);                      /* room_speed: game steps per second */
int  ge_room_inst_count(const ge_room_t *r);
ge_inst_t *ge_room_inst(ge_room_t *r, int i);              /* instance i of the room (start state from the room data) */
void ge_room_inst_set_depth(ge_room_t *r, int i, int depth);   /* GM depth (larger = drawn earlier); re-sorts lazily */
void ge_room_step(ge_room_t *r);                           /* one game step: sprite animation (image_index += image_speed * speed) */
void ge_room_draw(const ge_room_t *r, int cam_x, int cam_y);
int  ge_room_missing_tex(const ge_room_t *r);              /* textures that did not fit in VRAM */
void ge_room_size(const ge_room_t *r, int *w, int *h);
void ge_room_stats(const ge_room_t *r, int *assets, int *items);

int  ge_pack_sprite_count(const ge_pack_t *p);
const ge_sprite_t *ge_pack_sprite(const ge_pack_t *p, int i);
const ge_frame_t  *ge_pack_frame(const ge_pack_t *p, const ge_sprite_t *s, int f);
int  ge_pack_bg_count(const ge_pack_t *p);
const ge_bg_t     *ge_pack_bg(const ge_pack_t *p, int i);
int  ge_pack_font_count(const ge_pack_t *p);
const ge_font_t   *ge_pack_font(const ge_pack_t *p, int i);
const ge_glyph_t  *ge_pack_glyph(const ge_pack_t *p, const ge_font_t *f, unsigned ch);   /* NULL if the font has no such char */
ge_tex_t ge_pack_tex(ge_pack_t *p, uint32_t asset);       /* texture handle for a global asset; valid until ge_pack_frame_end */
void     ge_pack_frame_end(ge_pack_t *p);                 /* call after ge_gfx_frame_end: drops the references taken by ge_pack_tex */
/* room layers (tiles, backgrounds) in draw order; sprite instances of the blob are ignored (the GML runtime owns instances) */
int      ge_room_item_count(const ge_room_t *r);
int      ge_room_item_key(const ge_room_t *r, int oi);    /* ascending = drawn earlier; same scale as -depth */
void     ge_room_draw_item(const ge_room_t *r, int oi, int cam_x, int cam_y);
uint32_t ge_room_bg_color(const ge_room_t *r);
int      ge_room_item_bgslot(const ge_room_t *r, int oi, int *initially_on);   /* room background slot (background_visible index) of an item, -1 for tiles/others */
#endif
