#!/usr/bin/python3
"""gmrview.py <pack.gmpk> <room> <out.png>   software render of a packed room exactly as the GS path would draw it
(CLUT lookup, alpha 0x80=opaque, items back to front). Used to diff the pack against gmroom.py's reference render."""
import struct, sys
from PIL import Image


_P = {}      # pack path -> (mmap, header tuple, {room: (off, len)}, {asset id: decoded image cache})
_cur = None


def _open(pack):
    import mmap
    if pack not in _P:
        f = open(pack, "rb"); b = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        magic, ver, n, io, il, na, ao, al_, do, tot = struct.unpack_from("<4sIIIIIIIII", b, 0)
        assert ver == 3
        rooms = {}
        for k in range(n):
            nm, off, ln, w, h = struct.unpack_from("<32sIIII", b, io + 48 * k)
            rooms[nm.rstrip(b"\0").decode()] = (off, ln)
        _P[pack] = (b, (na, ao), rooms, {})
    return _P[pack]


def room_names(pack):
    return list(_open(pack)[2])


def read_room(pack, name):
    global _cur
    b, _, rooms, _ = _cur = _open(pack)
    if name not in rooms:
        raise KeyError(name)
    off, ln = rooms[name]
    return bytes(b[off:off + ln])


def _asset(b, ao, aid, cache):
    if aid in cache:
        return cache[aid]
    kind, pid, wh, stride, cl, dl, off, _ = struct.unpack_from("<8I", b, ao + 32 * aid)
    psm = pid & 255
    w, h = wh & 0xffff, wh >> 16
    co = off
    do = off + (cl + 15) // 16 * 16
    pal = [tuple(b[co + i * 4:co + i * 4 + 4]) for i in range(cl // 4)]
    if psm == 1:   # undo CSM1 swizzle of bits 3/4
        pal = [pal[(i & 0xE7) | ((i & 8) << 1) | ((i & 16) >> 1)] for i in range(256)]
    px = bytes(b[do:do + dl])
    im = Image.new("RGBA", (stride, h))
    data = []
    for y in range(h):
        for x in range(stride):
            if psm == 0:
                bb = px[y * (stride // 2) + x // 2]
                i = (bb >> 4) if x & 1 else (bb & 15)
            else:
                i = px[y * stride + x]
            r, g, bl, al = pal[i]
            data.append((r, g, bl, min(255, al * 2)))
    im.putdata(data)
    cache[aid] = im
    return im


def render(blob):
    from gmitems import bg_repeats, blit_xf, TILE_MIRROR, TILE_FLIP, TILE_ROTATE, TILE_IDX
    m, rw, rh, c, na, ni, oa, oi, od, tot, vw, vh, ninst, oinst, nspr, ospr, ofrm, rspeed = struct.unpack_from("<4sIIIIIIIIIIIIIIIII", blob, 0)
    assert m == b"GMR4"
    pb, (_, ao), _, cache = _cur
    ids = struct.unpack_from(f"<{na}I", blob, oa)

    class Lazy:
        def __getitem__(self, i):
            return _asset(pb, ao, ids[i], cache)
    tex = Lazy()
    img = Image.new("RGBA", (rw, rh), (c & 255, (c >> 8) & 255, (c >> 16) & 255, 255))
    for k in range(ni):
        ax, ay, rx, ry, sx, sy, w, h, a, fl, scx, scy, rot, col, skey, aux = struct.unpack_from("<hhhhHHHHHHfffIii", blob, oi + 44 * k)
        if fl & 16:      # animated instance: draw its start state (image_index at load)
            x, y, isx, isy, irot, icol, isp, idx, sl, vis = struct.unpack_from("<fffffIffiI", blob, oinst + 40 * aux)
            first, nf, spd = struct.unpack_from("<IHxxf", blob, ospr + 12 * sl)
            fa, fw, fh, frx, fry, _ = struct.unpack_from("<HHHhhH", blob, ofrm + 12 * (first + int(idx) % nf))
            if fw and vis:
                it = dict(ax=x, ay=y, rx=frx, ry=fry, w=fw, h=fh, scx=isx, scy=isy, rot=irot, color=icol, tx=0, ty=0)
                img = blit_xf(img, tex[fa].crop((0, 0, fw, fh)), it, x, y)
            continue
        if fl & 8:      # tile grid: decode exactly like the engine (visible-region logic is a pure optimisation)
            tx, ty, cols, rpb, cpb = sx, sy, int(scx), int(scy), int(rot)
            nbc = -(-cols // cpb) if cpb else 1
            tiles = struct.unpack_from(f"<{tx * ty}I", blob, col)
            for n, v in enumerate(tiles):
                if v == 0:
                    continue
                idx = v & TILE_IDX
                cx, cy = n % tx, n // tx
                cc, rr = idx % cols, idx // cols
                band = (rr // rpb if rpb else 0) * nbc + (cc // cpb if cpb else 0)
                ssy = ry + (rr % rpb if rpb else rr) * (h + 2 * ry)
                ssx = rx + (cc % cpb if cpb else cc) * (w + 2 * rx)
                it = dict(ax=0, ay=0, rx=0, ry=0, w=w, h=h, scx=1.0, scy=1.0, rot=0.0, color=0xFFFFFFFF, tx=0, ty=0)
                px_, py_ = ax + cx * w, ay + cy * h
                if v & (TILE_MIRROR | TILE_FLIP | TILE_ROTATE):
                    px_ += w / 2.0; py_ += h / 2.0; it["rx"] = -w / 2.0; it["ry"] = -h / 2.0
                    if v & TILE_MIRROR: it["scx"] = -1.0
                    if v & TILE_FLIP: it["scy"] = -1.0
                    if v & TILE_ROTATE: it["rot"] = -90.0
                sub = tex[a + band].crop((ssx, ssy, ssx + w, ssy + h))
                img = blit_xf(img, sub, it, px_, py_)
            continue
        it = dict(ax=ax, ay=ay, rx=rx, ry=ry, w=w, h=h, scx=scx, scy=scy, rot=rot, color=col, tx=fl & 1, ty=(fl >> 1) & 1)
        sub = tex[a].crop((sx, sy, sx + w, sy + h))
        pos = bg_repeats(it, rw, rh) if fl & 4 else [(ax, ay)]
        for (x, y) in pos:
            img = blit_xf(img, sub, it, x, y)
    return img


if __name__ == "__main__":
    render(read_room(sys.argv[1], sys.argv[2])).save(sys.argv[3])
