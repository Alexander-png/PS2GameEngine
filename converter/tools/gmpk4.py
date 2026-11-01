#!/usr/bin/python3
"""gmpk4.py <data.win> <texdir> <game> <out.gmpk>

GMPK v4 = v3 (rooms + shared asset table, see gmpk_rooms.py) plus global tables for the GML runtime, which draws any
sprite / background / font at run time (not only what the room blobs place):
  header 80 B: v3 fields (magic,ver=4,n_rooms,index_off,index_len,n_assets,aindex_off,aindex_len,data_off,total) then
               u32 spr_off, n_spr, frm_off, n_frm, bg_off, n_bg, font_off, n_font, glyph_off, n_glyph
  sprites  n_spr x 12 B : u32 first frame, u16 n frames, u16 pad, f32 image steps per game step at image_speed 1
  frames   n_frm x 12 B : u32 global asset id (0xFFFFFFFF = none), u16 w, u16 h, s16 rx, s16 ry  (offset of the frame image from the sprite origin)
  bgs      n_bg  x 12 B : u32 asset id (or -1), u16 w, u16 h, u32 pad
  fonts    n_font x 24 B: u32 asset id (glyph atlas), u16 atlas w, h, u32 first glyph, u16 n glyphs, u16 line height, u16 first char, u16 last char, u16 em size, u16 pad
  glyphs   n_glyph x 16 B: u16 char, x, y, w, h (position in the atlas), s16 shift (advance), s16 offset (x bearing), u16 pad
Tables are 16-byte aligned and sit between the asset index and the room blobs.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from gmpk_rooms import Store, SECT, al
from roompack import Ctx, build_room
from gmwin import DataWin

NONE = 0xFFFFFFFF


def clut8_order(i):
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)


def quant_one(im):
    """One RGBA image -> (psm, clut, w, h, stride, pixels); same rules as gs_convert.convert (exact palette when it fits)."""
    cols = im.getcolors(maxcolors=1 << 20)
    if cols is not None and len(cols) <= 256:
        pal = sorted(px for n, px in cols)
        lut = {px: i for i, px in enumerate(pal)}
        idx = [lut[p] for p in im.getdata()]
    else:
        q = im.quantize(colors=256, method=Image.FASTOCTREE, dither=Image.NONE)
        raw = q.getpalette(rawmode="RGBA") or []
        pal = [tuple(raw[i * 4:i * 4 + 4]) for i in range(256)]
        idx = list(q.getdata())
    t4 = len(pal) <= 16
    n = 16 if t4 else 256
    pal = (list(pal) + [(0, 0, 0, 0)] * n)[:n]
    entries = pal if t4 else [pal[clut8_order(i)] for i in range(n)]
    clut = bytearray()
    for r, g, b, a in entries:
        clut += bytes((r, g, b, (a * 128 + 127) // 255))
    w, h = im.size
    quant = 32 if t4 else 16
    stride = (w + quant - 1) // quant * quant
    buf = bytearray()
    for y in range(h):
        row = idx[y * w:(y + 1) * w] + [0] * (stride - w)
        if t4:
            buf += bytes((row[i] & 15) | ((row[i + 1] & 15) << 4) for i in range(0, stride, 2))
        else:
            buf += bytes(row)
    return (0 if t4 else 1), bytes(clut), w, h, stride, bytes(buf)


def main():
    win, texdir, game, out = sys.argv[1:5]
    C = Ctx(win, game)
    dw = C.dw
    S = Store()
    blobs, fails = [], []
    for r in C.names:
        try:
            b, ni, na = build_room(C, r, S)
            blobs.append((r, b))
        except Exception as e:
            fails.append((r, str(e)[:60]))

    # ---- sprites
    spr_rec, frm_rec = [], []
    sprs, tpd = dw.sprites(), dw.tpag_entries()
    for sid, sp in enumerate(sprs):
        spd = sp["speed"] / 30.0 if sp.get("speed_type", 1) == 0 else sp["speed"]
        first = len(frm_rec)
        for f, tpo in enumerate(sp["frames"]):
            t = tpd.get(tpo)
            if t and (0, sid) in C.idx:
                psm, clut, w, h, stride, px = C.load((0, sid, f))
                a = S.add(0, psm, w, h, stride, clut, px, sid)
                frm_rec.append((a, t[2], t[3], max(-32768, min(32767, t[4] - sp["ox"])), max(-32768, min(32767, t[5] - sp["oy"]))))
            else:
                frm_rec.append((NONE, 0, 0, 0, 0))
        spr_rec.append((first, len(sp["frames"]), float(spd)))

    # ---- backgrounds
    bg_rec = []
    tpe = dw.tpag_entries()
    for bi, b in enumerate(dw.backgrounds()):
        tpt = tpe.get(b["tpag"]) or (0,) * 11
        bgoff = (tpt[4] & 0xFFFF) | ((tpt[5] & 0xFFFF) << 16)          # trimmed texture: where its pixels sit in the untrimmed image
        if (1, bi) in C.idx:
            C.bandinfo = {}
            h = C.height((1, bi))
            if h[1] <= 1024:
                psm, clut, w, hh, stride, px = C.load((1, bi))
                bg_rec.append((S.add(1, psm, w, hh, stride, clut, px, bi), w, hh, bgoff))
                continue
        bg_rec.append((NONE, 0, 0, 0))

    # ---- fonts (glyph atlas = the font's texture-page rectangle)
    font_rec, glyph_rec = [], []
    pages = {}

    def page(n):
        if n not in pages:
            pages[n] = Image.open(os.path.join(texdir, f"texture_{n:03d}.png")).convert("RGBA")
        return pages[n]
    # Some data.win files list one more texture page than the extractor finds, so fonts that own a whole page refer to
    # page n while the file is n-1: pick the shift (0, -1, +1) that makes the most whole-page fonts match their file size.
    def font_geom(p):
        tp = dw.u32(p + 28)
        return struct.unpack_from("<11H", dw.buf, tp)
    best = (-1, 0)
    for sh in (0, -1, 1):
        ok = 0
        for p in dw.ptr_list("FONT"):
            x, y, w, h, tx, ty, tw, th, bw, bh, pg = font_geom(p)
            if x == 0 and y == 0 and (w, h) == (bw, bh) and os.path.exists(os.path.join(texdir, f"texture_{pg + sh:03d}.png")):
                ok += page(pg + sh).size == (bw, bh)
        if ok > best[0]:
            best = (ok, sh)
    font_shift = best[1]
    print("font page shift", font_shift)
    for p in dw.ptr_list("FONT"):
        name = dw.string_at(dw.u32(p))
        em = dw.u32(p + 8)
        rs = dw.u32(p + 20)
        first_ch, last_ch = rs & 0xFFFF, dw.u32(p + 24)
        tp_ptr = dw.u32(p + 28)
        x, y, w, h, tx, ty, tw, th, bw, bh, pg = struct.unpack_from("<11H", dw.buf, tp_ptr)
        if x == 0 and y == 0 and (w, h) == (bw, bh):
            pg += font_shift
        try:
            im = page(pg).crop((x, y, x + w, y + h))
        except FileNotFoundError:
            print(f"font {name}: texture page {pg} missing, no glyphs")
            font_rec.append((NONE, 0, 0, len(glyph_rec), 0, 0, 0, 0, em))
            continue
        psm, clut, aw, ah, stride, px = quant_one(im)
        asset = S.add(4, psm, aw, ah, stride, clut, px, 0)
        g0 = len(glyph_rec)
        gl = dw._plist(p + 40)
        lineh = 0
        for g in gl:
            ch, gx, gy, gw, gh = struct.unpack_from("<5H", dw.buf, g)
            shift, off = struct.unpack_from("<2h", dw.buf, g + 10)
            glyph_rec.append((ch, gx, gy, gw, gh, shift, off))
            lineh = max(lineh, gh)
        font_rec.append((asset, aw, ah, g0, len(gl), lineh, first_ch, min(last_ch, 65535), em))
        print(f"font {name}: atlas {aw}x{ah} T{'4' if psm == 0 else '8'}, {len(gl)} glyphs, line {lineh}")

    # ---- layout
    n, na = len(blobs), len(S.ents)
    index_off = SECT
    index_len = al(n * 48)
    aindex_off = index_off + index_len
    aindex_len = al(na * 32)
    tab_off = aindex_off + aindex_len
    a16 = lambda v: al(v, 16)
    spr_off = tab_off
    frm_off = a16(spr_off + 12 * len(spr_rec))
    bg_off = a16(frm_off + 12 * len(frm_rec))
    font_off = a16(bg_off + 12 * len(bg_rec))
    glyph_off = a16(font_off + 24 * len(font_rec))
    tab_end = a16(glyph_off + 16 * len(glyph_rec))
    data_off = al(tab_end)
    ent, body, pos = b"", b"", data_off
    for r, b in blobs:
        w, h = struct.unpack_from("<II", b, 4)
        ent += struct.pack("<32sIIII", r.encode()[:31], pos, len(b), w, h)
        pad = al(len(b))
        body += b + b"\0" * (pad - len(b))
        pos += pad
    aent = b""
    for kind, pi, wh, stride, clut, px in S.ents:
        blk = clut + b"\0" * (al(len(clut), 16) - len(clut)) + px
        aent += struct.pack("<8I", kind, pi, wh, stride, len(clut), len(px), pos, 0)
        body += blk + b"\0" * (al(len(blk)) - len(blk))
        pos += al(len(blk))
    tab = bytearray(tab_end - tab_off)

    def put(off, data):
        tab[off - tab_off:off - tab_off + len(data)] = data
    put(spr_off, b"".join(struct.pack("<IHHf", f, nf, 0, s) for f, nf, s in spr_rec))
    put(frm_off, b"".join(struct.pack("<IHHhh", a, w, h, rx, ry) for a, w, h, rx, ry in frm_rec))
    put(bg_off, b"".join(struct.pack("<IHHI", a, w, h, o) for a, w, h, o in bg_rec))
    put(font_off, b"".join(struct.pack("<IHHIHHHHHH", a, w, h, g0, ng, lh, fc, lc, em, 0) for a, w, h, g0, ng, lh, fc, lc, em in font_rec))
    put(glyph_off, b"".join(struct.pack("<5Hh h H".replace(" ", ""), ch, gx, gy, gw, gh, sh, of, 0) for ch, gx, gy, gw, gh, sh, of in glyph_rec))
    hdr = struct.pack("<4sIIIIIIIII", b"GMPK", 4, n, index_off, index_len, na, aindex_off, aindex_len, data_off, pos)
    hdr += struct.pack("<10I", spr_off, len(spr_rec), frm_off, len(frm_rec), bg_off, len(bg_rec), font_off, len(font_rec), glyph_off, len(glyph_rec))
    with open(out, "wb") as f:
        f.write(hdr.ljust(SECT, b"\0"))
        f.write(ent.ljust(index_len, b"\0"))
        f.write(aent.ljust(aindex_len, b"\0"))
        f.write(bytes(tab))
        f.write(b"\0" * (data_off - tab_end))
        f.write(body)
    print(f"{game}: {n} rooms ({len(fails)} failed), {na} unique assets for {S.refs} refs, {len(spr_rec)} sprites, {len(frm_rec)} frames, "
          f"{len(bg_rec)} bgs, {len(font_rec)} fonts -> {pos / 1048576:.1f} MB")
    for r, e in fails[:10]:
        print("  FAIL", r, e)


if __name__ == "__main__":
    main()
