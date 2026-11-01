#!/usr/bin/python3
"""gmroom.py: full room geometry from a GMS1-style ROOM chunk (Undertale 1.0u3; GMS2 layered rooms come later).

API:   parse_room(dw, index) -> dict(name,w,h,speed,color,bgs[],views[],instances[],tiles[])
CLI:   gmroom.py <data.win> <texdir> <room_name> [out.png]   renders the room (tiles + visible instance sprites, depth sorted)
Entry layouts (word = u32 at base + 4*n):
  background (0x28): enabled, foreground, bg id, x, y, tileX, tileY, speedX, speedY, stretch
  view       (0x38): enabled, x, y, w, h, portX, portY, portW, portH, borderX, borderY, speedX, speedY, followObj
  instance   (0x2C): x, y, object, id, creation code, scaleX f, scaleY f, color(ABGR), rotation f
  tile       (0x30): x, y, bg, srcX, srcY, w, h, depth, id, scaleX f, scaleY f, color
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin


def parse_room(dw, ri):
    p = dw.ptr_list("ROOM")[ri]
    u, i, f = dw.u32, dw.i32, dw.f32
    r = dict(name=dw.string_at(u(p)), w=u(p + 8), h=u(p + 12), speed=u(p + 16), persistent=u(p + 20), color=u(p + 24),
             draw_color=u(p + 28), creation_code=i(p + 32), bgs=[], views=[], instances=[], tiles=[])
    bgs_p, views_p, objs_p, tiles_p = (u(p + 4 * k) for k in (10, 11, 12, 13))
    for b in dw._plist(bgs_p):
        r["bgs"].append(dict(enabled=u(b), foreground=u(b + 4), bg=i(b + 8), x=i(b + 12), y=i(b + 16), tile_x=u(b + 20), tile_y=u(b + 24),
                             speed_x=i(b + 28), speed_y=i(b + 32), stretch=u(b + 36)))
    for v in dw._plist(views_p):
        r["views"].append(dict(enabled=u(v), x=i(v + 4), y=i(v + 8), w=u(v + 12), h=u(v + 16), px=i(v + 20), py=i(v + 24), pw=u(v + 28),
                               ph=u(v + 32), bx=i(v + 36), by=i(v + 40), sx=i(v + 44), sy=i(v + 48), follow=i(v + 52)))
    for o in dw._plist(objs_p):
        r["instances"].append(dict(x=i(o), y=i(o + 4), obj=i(o + 8), id=u(o + 12), code=i(o + 16), sx=f(o + 20), sy=f(o + 24), color=u(o + 28), rot=f(o + 32)))
    for t in dw._plist(tiles_p):
        r["tiles"].append(dict(x=i(t), y=i(t + 4), bg=i(t + 8), sx=u(t + 12), sy=u(t + 16), w=u(t + 20), h=u(t + 24), depth=i(t + 28), id=u(t + 32),
                               scx=f(t + 36), scy=f(t + 40), color=u(t + 44)))
    return r


def object_visible(dw, oi):
    p = dw.ptr_list("OBJT")[oi]
    return dw.u32(p + 8) != 0


def main():
    from PIL import Image
    win, texdir, name = sys.argv[1:4]
    out = sys.argv[4] if len(sys.argv) > 4 else f"/tmp/{name}.png"
    dw = DataWin(win)
    names = [dw.string_at(dw.u32(p)) for p in dw.ptr_list("ROOM")]
    objs, sprs, tp, bgs = dw.objects(), dw.sprites(), dw.tpag_entries(), dw.backgrounds()
    pages = {}

    def crop(t):
        n = t[10]
        if n not in pages:
            pages[n] = Image.open(os.path.join(texdir, f"texture_{n:03d}.png")).convert("RGBA")
        return pages[n].crop((t[0], t[1], t[0] + t[2], t[1] + t[3])), t

    from gmitems import room_items, bg_repeats, blit_xf
    from gmitems import room_model, bg_repeats, blit_xf, expand_grid
    m = room_model(dw, names.index(name))
    c = m["color"]
    img = Image.new("RGBA", (m["w"], m["h"]), (c & 255, (c >> 8) & 255, (c >> 16) & 255, 255))
    ndraw = 0
    for it0 in m["items"]:
        for it in (expand_grid(it0) if it0["kind"] == "grid" else [it0]):
            kind, id_ = it["key"][:2]
            if kind == 1:
                t = tp.get(bgs[id_]["tpag"])
            else:
                t = tp.get(sprs[id_]["frames"][it["key"][2]])
            if not t:
                continue
            im, _ = crop(t)
            sub = im.crop((it["sx"], it["sy"], it["sx"] + it["w"], it["sy"] + it["h"]))
            pos = bg_repeats(it, m["w"], m["h"]) if it["kind"] == "bg" else [(it["ax"], it["ay"])]
            for (x, y) in pos:
                img = blit_xf(img, sub, it, x, y)
                ndraw += 1
    img.save(out)
    print(f"{name}: {m['w']}x{m['h']} items={len(m['items'])} drawn={ndraw} -> {out}")


if __name__ == "__main__":
    main()
