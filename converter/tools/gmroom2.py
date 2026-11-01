"""gmroom2.py: GMS2 (bytecode 16/17) ROOM parser with layers.

parse_room2(dw, index) -> dict(name,w,h,color,flags,views[],objs{id:dict},layers[])
layer: name,id,type(1 bg,2 instances,3 assets,4 tiles,5 effect),depth,xo,yo,hs,vs,visible,
       type 1: bg=dict(visible,fg,sprite,htile,vtile,stretch,color,frame,speed)
       type 2: ids=[instance ids]
       type 3: tiles=[legacy tile dicts], sprites=[sprite instance dicts]
       type 4: ts (tileset index), tx, ty, tiles=[u32 ...]
Tileset geometry from BGND (GMS2 extension): tw, th, bx, by, cols, count.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def tilesets(dw):
    """per BGND index: dict(name, tpag, tw, th, bx, by, cols, count)"""
    out = []
    for p in dw.ptr_list("BGND"):
        out.append(dict(name=dw.string_at(dw.u32(p)), tpag=dw.u32(p + 16), tw=dw.u32(p + 24), th=dw.u32(p + 28), bx=dw.u32(p + 32),
                        by=dw.u32(p + 36), cols=dw.u32(p + 40), count=dw.u32(p + 48)))
    return out


def parse_room2(dw, ri):
    p = dw.ptr_list("ROOM")[ri]
    u, i, f = dw.u32, dw.i32, dw.f32
    effects = any(c in dw.chunks for c in ("TGIN", "PSEM", "FEAT", "PSYS"))
    particles = any(c in dw.chunks for c in ("PSEM", "PSYS"))
    r = dict(name=dw.string_at(u(p)), w=u(p + 8), h=u(p + 12), speed=u(p + 16), persistent=u(p + 20), color=u(p + 24),
             draw_color=u(p + 28), creation_code=i(p + 32), flags=u(p + 36), views=[], objs={}, layers=[])
    wi = 15 if u(p + 56) > 1 else 14
    for v in dw._plist(u(p + 44)):
        r["views"].append(dict(enabled=u(v), x=i(v + 4), y=i(v + 8), w=u(v + 12), h=u(v + 16), px=i(v + 20), py=i(v + 24), pw=u(v + 28),
                               ph=u(v + 32), follow=i(v + 52)))
    for o in dw._plist(u(p + 48)):
        r["objs"][u(o + 12)] = dict(x=i(o), y=i(o + 4), obj=i(o + 8), id=u(o + 12), code=i(o + 16), sx=f(o + 20), sy=f(o + 24),
                                    speed=f(o + 28), frame=i(o + 32), color=u(o + 36), rot=f(o + 40))
    for lp in dw._plist(u(p + 4 * (wi + 8))):
        lt = u(lp + 8)
        L = dict(name=dw.string_at(u(lp)), id=u(lp + 4), type=lt, depth=i(lp + 12), xo=f(lp + 16), yo=f(lp + 20), hs=f(lp + 24),
                 vs=f(lp + 28), visible=u(lp + 32))
        d = lp + 0x30 + 12 * u(lp + 0x2C) if effects else lp + 0x24
        if lt == 1:
            L["bg"] = dict(visible=u(d), fg=u(d + 4), sprite=i(d + 8), htile=u(d + 12), vtile=u(d + 16), stretch=u(d + 20), color=u(d + 24),
                           frame=f(d + 28), speed=f(d + 32))
        elif lt == 2:
            n = u(d)
            L["ids"] = [u(d + 4 + 4 * k) for k in range(n)]
        elif lt == 3:
            L["tiles"] = [dict(x=i(t), y=i(t + 4), bg=i(t + 8), sx=u(t + 12), sy=u(t + 16), w=u(t + 20), h=u(t + 24), depth=i(t + 28),
                               id=u(t + 32), scx=f(t + 36), scy=f(t + 40), color=u(t + 44)) for t in dw._plist(u(d))]
            L["sprites"] = [dict(sprite=i(s + 4), x=i(s + 8), y=i(s + 12), scx=f(s + 16), scy=f(s + 20), color=u(s + 24), speed=f(s + 28),
                                 stype=u(s + 32), frame=f(s + 36), rot=f(s + 40)) for s in dw._plist(u(d + 4))]
        elif lt == 4:
            tx, ty = u(d + 4), u(d + 8)
            L.update(ts=i(d), tx=tx, ty=ty, tiles=list(struct.unpack_from(f"<{tx * ty}I", dw.buf, d + 12)) if tx * ty < 4000000 else [])
        r["layers"].append(L)
    return r
