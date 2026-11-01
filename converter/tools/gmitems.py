"""gmitems.py: the room -> draw item list (GameMaker semantics, shared by the reference renderer and the packer).

Draw order: layer 0 = non-foreground background layers, layer 1 = tiles + instances sorted by depth (larger depth first,
tiles before instances at equal depth), layer 2 = foreground background layers.
item dict: layer, depth, kind ('bg'|'tile'|'spr'), key=(1,bg id)|(0,sprite id), sx,sy,w,h (source rect inside that image),
           ax,ay (world anchor), rx,ry (offset of the source rect's top-left from the anchor, unscaled),
           scx,scy (scale), rot (deg, counterclockwise), color (GM ABGR: alpha in the top byte), tx,ty (bg repeat flags)
Static start state only: scroll speeds, image_index/speed, creation-code changes are runtime business.
"""
import math
from gmroom import parse_room, object_visible


def room_items(dw, r, objs, sprs, tp, bgs):
    items = []
    for slot, b in enumerate(r["bgs"]):
        if b["bg"] < 0:
            continue                 # (disabled layers are kept: the game turns them on with background_visible[slot])
        bg = bgs[b["bg"]]
        t = tp.get(bg["tpag"])
        if not t:
            continue
        w, h = t[2], t[3]
        ox, oy, bw, bh = t[4], t[5], t[8] or t[2], t[9] or t[3]      # trimmed texture: the w x h pixels sit at (ox,oy) inside a bw x bh image
        scx = scy = 1.0
        if b["stretch"]:
            scx, scy = r["w"] / bw, r["h"] / bh
        layer = 2 if b["foreground"] else 0
        common = dict(layer=layer, depth=0, kind="bg", key=(1, b["bg"]), sx=0, sy=0, w=w, h=h, rx=0, ry=0, scx=scx, scy=scy, rot=0.0,
                      color=0xFFFFFFFF, slot=slot, off=int(not b["enabled"]))
        if (ox, oy) == (0, 0) and (bw, bh) == (w, h):
            items.append(dict(common, ax=b["x"], ay=b["y"], tx=int(bool(b["tile_x"])), ty=int(bool(b["tile_y"]))))
            continue
        import math as _m
        def spans(base, period, tiled, limit):
            if not tiled:
                return [base]
            x = base - _m.ceil(base / period) * period
            out = []
            while x < limit and len(out) < 64:
                out.append(x)
                x += period
            return out
        for yy in spans(b["y"] + oy * scy, bh * scy, b["tile_y"], r["h"]):
            for xx in spans(b["x"] + ox * scx, bw * scx, b["tile_x"], r["w"]):
                items.append(dict(common, ax=xx, ay=yy, tx=0, ty=0))
    for t in r["tiles"]:
        if t["bg"] < 0 or t["bg"] >= len(bgs):
            continue
        sx, sy, w, h, ax, ay = t["sx"], t["sy"], t["w"], t["h"], t["x"], t["y"]
        tt = tp.get(bgs[t["bg"]]["tpag"])
        if tt and ((tt[4], tt[5]) != (0, 0) or (tt[8], tt[9]) != (tt[2], tt[3])):
            # trimmed texture: the tile's source rect is in the untrimmed image; keep the part that exists and shift it
            ox, oy, tw, th = tt[4], tt[5], tt[2], tt[3]
            x0, y0, x1, y1 = max(sx, ox), max(sy, oy), min(sx + w, ox + tw), min(sy + h, oy + th)
            if x1 <= x0 or y1 <= y0:
                continue
            ax += (x0 - sx) * t["scx"]; ay += (y0 - sy) * t["scy"]
            sx, sy, w, h = x0 - ox, y0 - oy, x1 - x0, y1 - y0
        items.append(dict(layer=1, depth=t["depth"], kind="tile", key=(1, t["bg"]), sx=sx, sy=sy, w=w, h=h,
                          ax=ax, ay=ay, rx=0, ry=0, scx=t["scx"], scy=t["scy"], rot=0.0, color=t["color"], tx=0, ty=0))
    for ins in r["instances"]:
        if ins["obj"] < 0 or ins["obj"] >= len(objs):
            continue
        o = objs[ins["obj"]]
        if o["sprite"] < 0 or not object_visible(dw, ins["obj"]):
            continue
        s = sprs[o["sprite"]]
        if not s["frames"]:
            continue
        t = tp.get(s["frames"][0])
        if not t:
            continue
        items.append(dict(layer=1, depth=o["depth"], kind="spr", key=(0, o["sprite"], 0), sx=0, sy=0, w=t[2], h=t[3],
                          ax=ins["x"], ay=ins["y"], rx=t[4] - s["ox"], ry=t[5] - s["oy"], scx=ins["sx"], scy=ins["sy"],
                          rot=ins["rot"], color=ins["color"], tx=0, ty=0, spid=o["sprite"], frame=0, speed=1.0))
    items.sort(key=lambda it: (it["layer"], -it["depth"] if it["layer"] == 1 else 0))
    return items


def bg_repeats(it, room_w, room_h):
    """positions (x, y) of every copy of a background layer item that touches the room"""
    w, h = it["w"] * it["scx"], it["h"] * it["scy"]
    xs, ys = [it["ax"]], [it["ay"]]
    if it["tx"] and w > 0:
        x0 = it["ax"] - math.ceil(it["ax"] / w) * w if it["ax"] > 0 else it["ax"]
        k0 = math.floor((0 - it["ax"]) / w)
        xs = [it["ax"] + k * w for k in range(k0, math.ceil((room_w - it["ax"]) / w) + 1)]
    if it["ty"] and h > 0:
        k0 = math.floor((0 - it["ay"]) / h)
        ys = [it["ay"] + k * h for k in range(k0, math.ceil((room_h - it["ay"]) / h) + 1)]
    return [(x, y) for y in ys for x in xs if x + w > 0 and y + h > 0 and x < room_w and y < room_h]


def blit_xf(canvas, src, it, ax, ay):
    """Composite RGBA `src` (the source rect image) onto `canvas` honoring scale/rotation/tint/alpha, anchor (ax, ay).
    Nearest sampling at pixel centres (same convention as the GS)."""
    from PIL import Image, ImageChops
    scx, scy, rot = it["scx"], it["scy"], it["rot"]
    c = it["color"]
    tint = (c & 255, (c >> 8) & 255, (c >> 16) & 255)
    alpha = (c >> 24) & 255
    if tint != (255, 255, 255):
        r, g, b, a = src.split()
        mul = lambda ch, m: ch.point(lambda v: (v * m + 127) // 255)
        src = Image.merge("RGBA", (mul(r, tint[0]), mul(g, tint[1]), mul(b, tint[2]), a))
    if alpha != 255:
        r, g, b, a = src.split()
        src = Image.merge("RGBA", (r, g, b, a.point(lambda v: (v * alpha + 127) // 255)))
    w, h = src.size
    if rot == 0 and scx == 1 and scy == 1:
        x, y = int(math.floor(ax + it["rx"] + 0.0)), int(math.floor(ay + it["ry"] + 0.0))
        layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        layer.paste(src, (x, y))
        return Image.alpha_composite(canvas, layer)
    a = math.radians(rot)
    ca, sa = math.cos(a), math.sin(a)
    # local (u, v) in source texels -> world: P = A + R(S*(r + (u, v)))   (R is CCW on screen => y flips)
    def fwd(u, v):
        dx, dy = (it["rx"] + u) * scx, (it["ry"] + v) * scy
        return ax + dx * ca + dy * sa, ay - dx * sa + dy * ca
    pts = [fwd(0, 0), fwd(w, 0), fwd(0, h), fwd(w, h)]
    x0, x1 = int(math.floor(min(p[0] for p in pts))), int(math.ceil(max(p[0] for p in pts)))
    y0, y1 = int(math.floor(min(p[1] for p in pts))), int(math.ceil(max(p[1] for p in pts)))
    x0, y0 = max(x0, 0), max(y0, 0)
    x1, y1 = min(x1, canvas.width), min(y1, canvas.height)
    if x1 <= x0 or y1 <= y0 or scx == 0 or scy == 0:
        return canvas
    # inverse: (X - ax, Y - ay) -> unrotate -> unscale -> minus r
    # dx =  (X-ax)*ca - (Y-ay)*sa ; dy = (X-ax)*sa + (Y-ay)*ca ; u = dx/scx - rx ; v = dy/scy - ry
    ia, ib = ca / scx, -sa / scx
    id_, ie = sa / scy, ca / scy
    # PIL AFFINE: input = (a*x + b*y + c, d*x + e*y + f) with x,y output-box pixel centres
    ox, oy = x0 - ax, y0 - ay
    cu = ia * ox + ib * oy - it["rx"]
    cv = id_ * ox + ie * oy - it["ry"]
    out = src.transform((x1 - x0, y1 - y0), Image.AFFINE, (ia, ib, cu, id_, ie, cv), Image.NEAREST)
    # transform() leaves outside-source pixels transparent (zeros) in RGBA - what we want
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    layer.paste(out, (x0, y0))
    return Image.alpha_composite(canvas, layer)


# ---------------------------------------------------------------- GMS2 (layers)
TILE_MIRROR, TILE_FLIP, TILE_ROTATE, TILE_IDX = 1 << 28, 1 << 29, 1 << 30, 0x7FFFF


def room_model(dw, ri):
    """-> dict(name,w,h,color,view_w,view_h,items[]) for GMS1 and GMS2 rooms; items are back-to-front. Item kinds:
    'bg' (repeatable), 'tile', 'spr' and, GMS2 only, 'grid' (a whole tile layer: tileset key, tx,ty,tw,th,bx,by,cols,tiles[])."""
    objs, sprs, tp, bgs = dw.objects(), dw.sprites(), dw.tpag_entries(), dw.backgrounds()
    if dw.gm_version[0] < 2:
        r = parse_room(dw, ri)
        items = _clip_all(room_items(dw, r, objs, sprs, tp, bgs), tp, sprs, bgs)
        views = [v for v in r["views"] if v["enabled"]]
        vw, vh = (views[0]["w"], views[0]["h"]) if views else (r["w"], r["h"])
        c = r["color"]
        return dict(name=r["name"], w=r["w"], h=r["h"], color=c & 0xFFFFFF, view_w=vw, view_h=vh, items=items, gms2=False, rspeed=r["speed"])
    from gmroom2 import parse_room2, tilesets
    r = parse_room2(dw, ri)
    ts = tilesets(dw)
    views = [v for v in r["views"] if v["enabled"]] if r["flags"] & 1 else []
    vw, vh = (views[0]["w"], views[0]["h"]) if views else (r["w"], r["h"])
    color = r["color"] & 0xFFFFFF
    items = []

    def spr_item(sid, frame, x, y, scx, scy, rot, col, layer_depth, speed=1.0):
        s = sprs[sid]
        if not s["frames"]:
            return
        fr = int(frame) % len(s["frames"])
        t = tp.get(s["frames"][fr])
        if not t:
            return
        items.append(dict(layer=0, depth=layer_depth, kind="spr", key=(0, sid, fr), sx=0, sy=0, w=t[2], h=t[3], ax=x, ay=y,
                          rx=t[4] - s["ox"], ry=t[5] - s["oy"], scx=scx, scy=scy, rot=rot, color=col, tx=0, ty=0, spid=sid, frame=fr, speed=speed))

    order = sorted(range(len(r["layers"])), key=lambda k: -r["layers"][k]["depth"])   # stable: file order within a depth
    for k in order:
        L = r["layers"][k]
        if not L["visible"]:
            continue
        lt = L["type"]
        if lt == 1:
            b = L["bg"]
            if not b["visible"]:
                continue
            if b["sprite"] < 0:
                if (b["color"] >> 24) & 255:      # solid colour layer = room background colour
                    color = b["color"] & 0xFFFFFF
                continue
            s = sprs[b["sprite"]]
            t = tp.get(s["frames"][0]) if s["frames"] else None
            if not t:
                continue
            scx = scy = 1.0
            if b["stretch"]:
                scx, scy = r["w"] / t[2], r["h"] / t[3]
            items.append(dict(layer=0, depth=L["depth"], kind="bg", key=(0, b["sprite"], 0), sx=0, sy=0, w=t[2], h=t[3], ax=L["xo"], ay=L["yo"],
                              rx=t[4], ry=t[5], scx=scx, scy=scy, rot=0.0, color=b["color"] | 0, tx=int(bool(b["htile"])), ty=int(bool(b["vtile"]))))
        elif lt == 2:
            for iid in L["ids"]:
                o = r["objs"].get(iid)
                if not o or o["obj"] < 0 or o["obj"] >= len(objs):
                    continue
                ob = objs[o["obj"]]
                if ob["sprite"] < 0 or not object_visible(dw, o["obj"]):
                    continue
                spr_item(ob["sprite"], o["frame"], o["x"], o["y"], o["sx"], o["sy"], o["rot"], o["color"], L["depth"], o["speed"])
        elif lt == 3:
            for t in L["tiles"]:     # legacy tiles: bg id is a sprite id in GMS2
                if t["bg"] < 0 or t["bg"] >= len(sprs) or not sprs[t["bg"]]["frames"]:
                    continue
                tt = tp.get(sprs[t["bg"]]["frames"][0])
                if not tt:
                    continue
                items.append(dict(layer=0, depth=L["depth"], kind="tile", key=(0, t["bg"], 0), sx=t["sx"], sy=t["sy"], w=t["w"], h=t["h"],
                                  ax=t["x"] + L["xo"], ay=t["y"] + L["yo"], rx=0, ry=0, scx=t["scx"], scy=t["scy"], rot=0.0, color=t["color"], tx=0, ty=0))
            for sp in L["sprites"]:
                if sp["sprite"] >= 0 and sp["sprite"] < len(sprs):
                    spr_item(sp["sprite"], sp["frame"], sp["x"], sp["y"], sp["scx"], sp["scy"], sp["rot"], sp["color"], L["depth"], sp["speed"])
        elif lt == 4 and L["ts"] >= 0 and L["tiles"]:
            t = ts[L["ts"]]
            if not tp.get(t["tpag"]):
                continue
            items.append(dict(layer=0, depth=L["depth"], kind="grid", key=(1, L["ts"]), sx=0, sy=0, w=t["tw"], h=t["th"], ax=L["xo"], ay=L["yo"],
                              rx=t["bx"], ry=t["by"], scx=float(t["cols"]), scy=1.0, rot=0.0, color=0xFFFFFFFF, tx=L["tx"], ty=L["ty"],
                              tiles=L["tiles"]))
    return dict(name=r["name"], w=r["w"], h=r["h"], color=color, view_w=vw, view_h=vh, items=_clip_all(items, tp, sprs, bgs), gms2=True, rspeed=r["speed"])


def _clip_all(items, tp, sprs, bgs):
    out = []
    for it in items:
        if it["kind"] == "tile":
            k = it["key"]
            t = tp.get(bgs[k[1]]["tpag"]) if k[0] == 1 else tp.get(sprs[k[1]]["frames"][k[2]])
            if not t:
                continue
            it = clip_tile(it, t[2], t[3])
            if it is None:
                continue
        out.append(it)
    return out


def expand_grid(it):
    """yield 'tile' items for every non-empty cell of a grid item (reference renderer / sw view use this)"""
    tw, th, bx, by, cols = it["w"], it["h"], it["rx"], it["ry"], int(it["scx"])
    tx = it["tx"]
    for n, v in enumerate(it["tiles"]):
        idx = v & TILE_IDX
        if v == 0:
            continue
        cx, cy = n % tx, n // tx
        col, row = idx % cols, idx // cols
        d = dict(layer=0, depth=it["depth"], kind="tile", key=it["key"], sx=bx + col * (tw + 2 * bx), sy=by + row * (th + 2 * by), w=tw, h=th,
                 ax=it["ax"] + cx * tw, ay=it["ay"] + cy * th, rx=0, ry=0, scx=1.0, scy=1.0, rot=0.0, color=0xFFFFFFFF, tx=0, ty=0)
        if v & (TILE_MIRROR | TILE_FLIP | TILE_ROTATE):
            d["ax"] += tw / 2.0; d["ay"] += th / 2.0; d["rx"] = -tw / 2.0; d["ry"] = -th / 2.0
            if v & TILE_MIRROR: d["scx"] = -1.0
            if v & TILE_FLIP: d["scy"] = -1.0
            if v & TILE_ROTATE: d["rot"] = -90.0
        yield d


def _s32(v):
    return v - (1 << 32) if v >= (1 << 31) else v


def clip_tile(it, iw, ih):
    """legacy tiles may name a source rect partly outside the image (negative origin / overhang): clip it, keep placement"""
    it["sx"], it["sy"] = _s32(it["sx"]), _s32(it["sy"])
    sx, sy, w, h = it["sx"], it["sy"], it["w"], it["h"]
    if sx >= 0 and sy >= 0 and sx + w <= iw and sy + h <= ih:
        return it
    nx0, ny0 = max(sx, 0), max(sy, 0)
    nx1, ny1 = min(sx + w, iw), min(sy + h, ih)
    if nx1 <= nx0 or ny1 <= ny0:
        return None
    it["ax"] += (nx0 - sx) * it["scx"]; it["ay"] += (ny0 - sy) * it["scy"]
    it["sx"], it["sy"], it["w"], it["h"] = nx0, ny0, nx1 - nx0, ny1 - ny0
    return it
