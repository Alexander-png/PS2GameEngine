#!/usr/bin/python3
"""gs_convert.py <data.win> <texdir> <game> [limit]

Converts sprites (all frames) and tilesets into GS-ready indexed payloads (v1):
  - palette per sprite shared by all frames: exact when <=16 / <=256 RGBA colors, else PIL octree quantization
    to 256 (lossy, error measured as mean abs RGB difference, alpha-weighted)
  - CLUT entries RGBA32 as the GS expects (alpha 0..0x80), CLUT8 in CSM1 order (index bits 3 and 4 swapped)
  - pixel data linear raster, PSMT4 (2 px/byte, first pixel in the low nibble) or PSMT8; row stride padded to a
    quadword (32 px for T4, 16 px for T8) so each row is an integral number of 16-byte quads for GIF IMAGE upload
  - NOT done yet: re-atlasing and the block-swizzled "upload as PSMCT32" optimisation (verify in PCSX2 first)
Outputs out/<game>.gsassets and out/<game>_gsreport.csv (real bytes vs the estimate used in the planning tools).
"""
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from PIL import Image, ImageChops, ImageStat

win, texdir, game = sys.argv[1:4]
LIMIT = int(sys.argv[4]) if len(sys.argv) > 4 else 0
dw = DataWin(win)
sprites, tp, bgs = dw.sprites(), dw.tpag_entries(), dw.backgrounds()

_cache = {}


def page(n):
    if n not in _cache:
        if len(_cache) >= 3:
            _cache.pop(next(iter(_cache)))
        _cache[n] = Image.open(os.path.join(texdir, f"texture_{n:03d}.png")).convert("RGBA")
    return _cache[n]


def crop(t):
    x, y, w, h = t[0], t[1], t[2], t[3]
    return page(t[10]).crop((x, y, x + w, y + h))


def pm(im):
    return Image.alpha_composite(Image.new("RGBA", im.size, (0, 0, 0, 255)), im).convert("RGB")


def clut8_order(i):
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)


def convert(frames):
    """frames: list of RGBA images. Returns (psm, palette[(r,g,b,a8)], [(w,h,stride,bytes)], lossy, err)."""
    # exact colors
    cols = {}
    for im in frames:
        c = im.getcolors(maxcolors=1 << 20)
        if c is None:
            cols = None
            break
        for n, px in c:
            cols[px] = 1
        if len(cols) > 256:
            break
    lossy, err = False, 0.0
    if cols is not None and len(cols) <= 256:
        pal = sorted(cols)
        lut = {px: i for i, px in enumerate(pal)}
        idx = [[lut[p] for p in im.getdata()] for im in frames]
    else:
        lossy = True
        w = max(im.width for im in frames)
        strip = Image.new("RGBA", (w, sum(im.height for im in frames)), (0, 0, 0, 0))
        y = 0
        for im in frames:
            strip.paste(im, (0, y))
            y += im.height
        q = strip.quantize(colors=256, method=Image.FASTOCTREE, dither=Image.NONE)
        raw = q.getpalette(rawmode="RGBA") or []
        pal = [tuple(raw[i * 4:i * 4 + 4]) for i in range(256)]
        idx, y, errs = [], 0, []
        for im in frames:
            part = q.crop((0, y, im.width, y + im.height))
            idx.append(list(part.getdata()))
            rec = Image.new("RGBA", im.size)
            rec.putdata([pal[i] for i in idx[-1]])
            d = ImageChops.difference(pm(rec), pm(im))
            errs.append(sum(ImageStat.Stat(d).mean) / 3.0)
            y += im.height
        err = max(errs)
    t4 = len(pal) <= 16
    psm = 0 if t4 else 1
    n = 16 if t4 else 256
    pal = (list(pal) + [(0, 0, 0, 0)] * n)[:n]
    clut = bytearray()
    entries = [pal[i] for i in range(n)] if t4 else [pal[clut8_order(i)] for i in range(n)]
    for r, g, b, a in entries:
        clut += bytes((r, g, b, (a * 128 + 127) // 255))
    out = []
    quant = 32 if t4 else 16
    for im, ix in zip(frames, idx):
        w, h = im.size
        stride = (w + quant - 1) // quant * quant
        buf = bytearray()
        for yy in range(h):
            row = ix[yy * w:(yy + 1) * w] + [0] * (stride - w)
            if t4:
                buf += bytes((row[i] & 15) | ((row[i + 1] & 15) << 4) for i in range(0, stride, 2))
            else:
                buf += bytes(row)
        out.append((w, h, stride, bytes(buf)))
    return psm, bytes(clut), out, lossy, err


items = []
for si, s in enumerate(sprites):
    fr = [tp.get(fp) for fp in s["frames"]]
    if fr and all(fr):
        items.append((0, si, s["name"], fr))
for bi, b in enumerate(bgs):
    t = tp.get(b["tpag"])
    if t:
        items.append((1, bi, b["name"], [t]))
items.sort(key=lambda it: it[3][0][10])
if LIMIT:
    items = items[:LIMIT]

rows, blobs = [], []
for kind, i, name, fr in items:
    try:
        frames = [crop(t) for t in fr]
        psm, clut, fdata, lossy, err = convert(frames)
    except Exception as e:  # keep going; report
        rows.append((kind, i, name, "ERR", 0, 0, 0, 0, 0.0, str(e)[:60]))
        continue
    body = struct.pack("<IBBHII", i, kind, psm, len(fdata), len(clut), 0) + clut
    for w, h, stride, data in fdata:
        body += struct.pack("<HHHHI", w, h, stride, 0, len(data)) + data
    blobs.append((kind, i, body))
    est = sum(t[2] * t[3] for t in fr) * (0.5 if psm == 0 else 1.0) + len(clut)
    rows.append((kind, i, name, "T4" if psm == 0 else "T8", len(fdata), int(lossy), len(body), int(est), round(err, 2), ""))

with open(f"out/{game}.gsassets", "wb") as f:
    f.write(struct.pack("<4sII", b"GSAS", 1, len(blobs)))
    off = 12 + 16 * len(blobs)
    for kind, i, body in blobs:
        f.write(struct.pack("<IIII", kind, i, off, len(body)))
        off += len(body)
    for _, _, body in blobs:
        f.write(body)
with open(f"out/{game}_gsreport.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["kind", "id", "name", "psm", "frames", "lossy", "bytes", "est_bytes", "max_err", "note"])
    w.writerows(rows)
ok = [r for r in rows if r[3] != "ERR"]
lossy = [r for r in ok if r[5]]
errs = sorted(r[8] for r in lossy)
print(f"{game}: converted {len(ok)} of {len(rows)} assets; real {sum(r[6] for r in ok)/1048576:.1f} MB vs estimate "
      f"{sum(r[7] for r in ok)/1048576:.1f} MB; lossy {len(lossy)}"
      + (f" (err median {errs[len(errs)//2]:.1f}, p90 {errs[int(.9*len(errs))]:.1f}, max {errs[-1]:.1f} /255)" if errs else ""))
