#!/usr/bin/python3
"""roompack.py <data.win> <texdir> <game> <room> <out.gmr>

Room blob builder (GMR4; assets live in the pack's shared asset table, see gmpk_rooms.py). Reads out/<game>.gsassets (gs_convert) and the room model (gmitems.room_model, GMS1 and GMS2).
Layout (little endian, 16-byte aligned sections):
  header 80 B: 'GMR4', room w, h, bg RGBA, n_assets (refs), n_items, off_assets (u32 global asset ids), off_items, off_data (grid tile arrays), total, view_w, view_h
  item   40 B: s16 ax ay rx ry; u16 sx sy w h asset flags; f32 scx scy rot; u32 color   (+4 pad); back to front
      flags: 1 repeat x, 2 repeat y, 4 background layer, 8 tile grid (ax,ay layer offset; sx,sy = tiles x,y; w,h tile size;
             rx,ry = tileset border x,y; scx = columns; scy = rows per texture band (0 = single), rot = columns per band (0 = single); color = data offset
             of the u32 tile array; `asset` = first band texture, bands are consecutive)
Sprites use the frame the room selects (instance image_index), no runtime animation yet.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin

BAND = 1024
ITEM_FMT = "<hhhhHHHHHHfffIii"   # ax ay rx ry sx sy w h asset flags scx scy rot color sortkey aux = 44 B
ITEM_SIZE = 44
INST_FMT = "<fffffIffiI"   # x y xscale yscale angle color image_speed image_index sprite visible = 40 B
SPR_FMT = "<IHxxf"          # first frame record, n frames, image-step per game step at image_speed 1 = 12 B... (packed 12)
FRM_FMT = "<HHHhhH"         # asset(room-local) w h rx ry pad = 12 B
F_TX, F_TY, F_BG, F_GRID, F_INST = 1, 2, 4, 8, 16
F_BGSLOT, F_BGOFF = 32, 64        # background layer with a room slot in aux (background_visible[slot]); initially hidden


class Ctx:
    def __init__(self, win, game):
        self.dw = DataWin(win)
        self.names = [self.dw.string_at(self.dw.u32(p)) for p in self.dw.ptr_list("ROOM")]
        self.gs = open(f"out/{game}.gsassets", "rb").read()
        magic, ver, n = struct.unpack_from("<4sII", self.gs, 0)
        self.idx = {}
        for k in range(n):
            kind, i, off, ln = struct.unpack_from("<IIII", self.gs, 12 + 16 * k)
            self.idx[(kind, i)] = (off, ln)
        self.bandinfo = {}

    def load(self, key):
        """key = (0, sprite, frame) | (1, bg) | (1, bg, band)"""
        gs = self.gs
        off, ln = self.idx[key[:2]]
        i, kind, psm, nf, clen, _ = struct.unpack_from("<IBBHII", gs, off)
        p = off + 16
        clut = gs[p:p + clen]
        p += clen
        want = key[2] if key[0] == 0 else 0
        for fr in range(nf):
            w, h, stride, _, dl = struct.unpack_from("<HHHHI", gs, p)
            if fr == want:
                break
            p += 12 + dl
        px = gs[p + 12:p + 12 + dl]
        if key[0] == 1 and len(key) > 2:
            bh, bw, nbc = self.bandinfo.get(key[:2], (BAND, 0, 1))
            rb, cb = key[2] // nbc, key[2] % nbc
            rbytes = stride // 2 if psm == 0 else stride
            y0, y1 = rb * bh, min((rb + 1) * bh, h)
            if bw:        # column band: repack every row with the band's own padded stride
                x0, x1 = cb * bw, min((cb + 1) * bw, w)
                quant = 32 if psm == 0 else 16
                nstride = (x1 - x0 + quant - 1) // quant * quant
                out = bytearray()
                for y in range(y0, y1):
                    row = px[y * rbytes:(y + 1) * rbytes]
                    if psm == 0:
                        idx = [(row[x // 2] >> (4 * (x & 1))) & 15 for x in range(x0, x1)] + [0] * (nstride - (x1 - x0))
                        out += bytes(idx[i] | (idx[i + 1] << 4) for i in range(0, nstride, 2))
                    else:
                        out += row[x0:x1] + b"\0" * (nstride - (x1 - x0))
                px, w, stride = bytes(out), x1 - x0, nstride
            else:
                px = px[y0 * rbytes:y1 * rbytes]
            h = y1 - y0
        return psm, clut, w, h, stride, px

    def height(self, key):
        off, ln = self.idx[key]
        i, kind, psm, nf, clen, _ = struct.unpack_from("<IBBHII", self.gs, off)
        return struct.unpack_from("<HH", self.gs, off + 16 + clen)


def build_room(C, room, store):
    from gmitems import room_model
    dw = C.dw
    m = room_model(dw, C.names.index(room))
    C.bandinfo = {}
    items = []
    keys = []

    def need(k):
        if k not in keys:
            keys.append(k)
        return keys.index(k)

    griddata = bytearray()
    sprs, tpd = dw.sprites(), dw.tpag_entries()
    spmap, spl, frames, insts = {}, [], [], []
    for it in m["items"]:
        k = it["key"]
        if k[:2] not in C.idx:
            continue
        if it["kind"] == "grid":
            iw_, ih_ = C.height(k)
            cell_w, cell_h = it["w"] + 2 * it["rx"], it["h"] + 2 * it["ry"]
            rpb = (BAND // cell_h) if ih_ > BAND else 0
            cpb = (BAND // cell_w) if iw_ > BAND else 0
            nbr = -(-ih_ // (rpb * cell_h)) if rpb else 1
            nbc = -(-iw_ // (cpb * cell_w)) if cpb else 1
            if rpb or cpb:
                C.bandinfo[k] = (rpb * cell_h if rpb else ih_, cpb * cell_w if cpb else 0, nbc)
                first = need((1, k[1], 0))
                for b in range(1, nbr * nbc):
                    assert need((1, k[1], b)) == first + b, "tileset bands not consecutive"
            else:
                first = need(k)
            d = dict(it); d["asset"] = first; d["rpb"] = rpb; d["cpb"] = cpb; d["rel"] = len(griddata)
            griddata += struct.pack(f"<{len(it['tiles'])}I", *it["tiles"])
            items.append(d)
            continue
        if k[0] == 1 and len(k) == 2:
            tw_, th_ = C.height(k)
            assert tw_ <= 1024, "texture wider than 1024 px"
            if th_ > BAND:
                assert it["rot"] == 0 and it["kind"] == "tile", "banded background layers not supported"
                sy, rem, y = it["sy"], it["h"], it["ay"]
                while rem > 0:        # split the tile at band borders
                    bnd = sy // BAND
                    take = min(rem, (bnd + 1) * BAND - sy)
                    dd = dict(it); dd["key"] = (1, k[1], bnd); dd["sy"] = sy - bnd * BAND; dd["h"] = take; dd["ay"] = y
                    dd["asset"] = need(dd["key"])
                    items.append(dd)
                    sy += take; y += take * it["scy"]; rem -= take
                continue
        if it["kind"] == "spr":
            sid = it["spid"]
            if sid not in spmap:
                sp = sprs[sid]
                spd = sp["speed"] / (m["rspeed"] or 30) if sp["speed_type"] == 0 else sp["speed"]
                first = len(frames)
                for f, tpo in enumerate(sp["frames"]):
                    t = tpd.get(tpo)
                    if t and (0, sid) in C.idx:
                        frames.append((need((0, sid, f)), t[2], t[3], t[4] - sp["ox"], t[5] - sp["oy"]))
                    else:
                        frames.append((0, 0, 0, 0, 0))
                spmap[sid] = len(spl)
                spl.append((first, len(sp["frames"]), spd))
            d = dict(it); d["asset"] = 0; d["inst"] = len(insts)
            insts.append((it["ax"], it["ay"], it["scx"], it["scy"], it["rot"], it["color"], it["speed"], float(it["frame"]), spmap[sid]))
            items.append(d)
            continue
        d = dict(it); d["asset"] = need(k)
        items.append(d)

    al = lambda n: (n + 15) // 16 * 16
    ids = []
    if not hasattr(C, "idcache"):
        C.idcache = {}
    for k in keys:
        ck = (k, C.bandinfo.get(k[:2]))
        if ck not in C.idcache:
            psm, clut, w, h, stride, px = C.load(k)
            C.idcache[ck] = store.add(k[0], psm, w, h, stride, clut, px, k[1])
        ids.append(C.idcache[ck])
    al16 = al
    data = bytearray(griddata)
    data += b"\0" * (al16(len(data)) - len(data))
    o_inst = len(data)
    for (x, y, sx, sy, rot, col, isp, fr, sl) in insts:
        data += struct.pack(INST_FMT, float(x), float(y), float(sx), float(sy), float(rot), col, float(isp), fr, sl, 1)
    o_spr = len(data)
    for (first, nf, spd) in spl:
        data += struct.pack(SPR_FMT, first, nf, float(spd))
    o_frm = len(data)
    for (a_, w_, h_, rx_, ry_) in frames:
        data += struct.pack(FRM_FMT, a_, w_, h_, max(-32768, min(32767, rx_)), max(-32768, min(32767, ry_)), 0)
    off_a = 80
    off_i = al(off_a + 4 * len(ids))
    off_d = al(off_i + ITEM_SIZE * len(items))
    gridoff = 0
    c = m["color"]
    hdr = struct.pack("<4sIIIIIIIIIIIIIIIII", b"GMR4", m["w"], m["h"], (c & 255) | (((c >> 8) & 255) << 8) | (((c >> 16) & 255) << 16) | 0x80000000,
                      len(ids), len(items), off_a, off_i, off_d, off_d + len(data), m["view_w"], m["view_h"],
                      len(insts), off_d + o_inst, len(spl), off_d + o_spr, off_d + o_frm, int(m["rspeed"] or 30))
    buf = bytearray(hdr.ljust(off_a, b"\0"))
    buf += struct.pack(f"<{len(ids)}I", *ids)
    buf += b"\0" * (off_i - len(buf))
    clamp = lambda v: max(-32768, min(32767, int(round(v))))
    BIG = 1 << 30

    def skey(it):
        if m["gms2"]:
            return -int(it["depth"])
        return -BIG if it["layer"] == 0 else (BIG if it["layer"] == 2 else -int(it["depth"]))
    for it in items:
        if it["kind"] == "grid":
            buf += struct.pack(ITEM_FMT, clamp(it["ax"]), clamp(it["ay"]), clamp(it["rx"]), clamp(it["ry"]), it["tx"], it["ty"], it["w"], it["h"],
                               it["asset"], F_GRID, it["scx"], float(it["rpb"]), float(it["cpb"]), off_d + gridoff + it["rel"], skey(it), -1)
            continue
        if it["kind"] == "spr":
            buf += struct.pack(ITEM_FMT, 0, 0, 0, 0, 0, 0, 0, 0, 0, F_INST, 1.0, 1.0, 0.0, 0, skey(it), it["inst"])
            continue
        fl = (F_TX if it["tx"] else 0) | (F_TY if it["ty"] else 0) | (F_BG if it["kind"] == "bg" else 0)
        slot = -1
        if it["kind"] == "bg" and "slot" in it:
            fl |= F_BGSLOT | (F_BGOFF if it["off"] else 0)
            slot = it["slot"]
        buf += struct.pack(ITEM_FMT, clamp(it["ax"]), clamp(it["ay"]), clamp(it["rx"]), clamp(it["ry"]), it["sx"], it["sy"], it["w"], it["h"],
                           it["asset"], fl, it["scx"], it["scy"], it["rot"], it["color"], skey(it), slot)
    buf += b"\0" * (off_d - len(buf))
    buf += data
    return bytes(buf), len(items), len(ids)
