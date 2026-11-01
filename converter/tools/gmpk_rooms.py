#!/usr/bin/python3
"""gmpk_rooms.py <data.win> <game> <out.gmpk> [room ...]

GMPK v3: room blobs + one shared, de-duplicated asset table. Little endian, 2048-byte sector aligned sections:
  sector 0   header 40 B: 'GMPK', ver=3, n_rooms, index_off, index_len, n_assets, aindex_off, aindex_len, data_off, total
  room index n_rooms x 48 B: name[32], blob_off, blob_len, w, h
  asset index n_assets x 32 B: kind, psm|id<<8, w|h<<16, stride, clut_len, data_len, off, pad
        the asset block at `off` (sector aligned) is clut (clut_len bytes, padded to 16) followed by the pixels (data_len)
  room blobs GMR4 (roompack.py): items + grid data + a u32 list of global asset ids; no pixel data
  asset data  each unique asset (identical bytes are stored once, also across rooms), ordered by first use
The runtime fetches an asset from the pack only when it is first drawn (streaming).
"""
import hashlib, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from roompack import Ctx, build_room

SECT = 2048
al = lambda n, a=SECT: (n + a - 1) // a * a


class Store:
    def __init__(self):
        self.ents, self.by_hash, self.refs = [], {}, 0

    def add(self, kind, psm, w, h, stride, clut, px, ident):
        self.refs += 1
        key = hashlib.sha1(struct.pack("<BBHHH", kind, psm, w, h, stride) + clut + b"|" + px).digest()
        if key in self.by_hash:
            return self.by_hash[key]
        self.by_hash[key] = len(self.ents)
        self.ents.append((kind, psm | (ident << 8), w | (h << 16), stride, clut, px))
        return len(self.ents) - 1


if __name__ == "__main__":
    win, game, out = sys.argv[1:4]
    want = sys.argv[4:]
    C = Ctx(win, game)
    rooms = want or C.names
    S = Store()
    blobs, fails = [], []
    for r in rooms:
        try:
            b, ni, na = build_room(C, r, S)
            blobs.append((r, b))
        except Exception as e:
            fails.append((r, str(e)[:60]))
    n, na = len(blobs), len(S.ents)
    index_off = SECT
    index_len = al(n * 48)
    aindex_off = index_off + index_len
    aindex_len = al(na * 32)
    data_off = aindex_off + aindex_len
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
    hdr = struct.pack("<4sIIIIIIIII", b"GMPK", 3, n, index_off, index_len, na, aindex_off, aindex_len, data_off, pos)
    with open(out, "wb") as f:
        f.write(hdr.ljust(SECT, b"\0"))
        f.write(ent.ljust(index_len, b"\0"))
        f.write(aent.ljust(aindex_len, b"\0"))
        f.write(body)
    sizes = sorted(len(b) for _, b in blobs) or [0]
    print(f"{game}: {n} rooms ({len(fails)} failed), {na} unique assets for {S.refs} references -> {pos/1048576:.1f} MB; "
          f"room blob KB median {sizes[n//2]//1024}, max {sizes[-1]//1024}")
    for r, e in fails[:10]:
        print("  FAIL", r, e)
