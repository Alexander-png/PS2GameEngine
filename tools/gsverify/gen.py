#!/usr/bin/python3
"""gen.py <file.gsassets> <gsreport.csv> <name_t4> <name_t8> <outdir>

Extracts two converted assets (first frame) from a .gsassets file into verify_data.h (C arrays for the GS test ELF)
and renders the expected image (palette applied, composited over magenta) as expected.png for comparison with a
PCSX2 screenshot.
"""
import csv
import struct
import sys
from PIL import Image

path, rep, n4, n8, out = sys.argv[1:6]
names = {r["name"]: r for r in csv.DictReader(open(rep)) if r["kind"] == "0"}
data = open(path, "rb").read()
magic, ver, n = struct.unpack_from("<4sII", data)
idx = {}
for k in range(n):
    kind, i, off, ln = struct.unpack_from("<IIII", data, 12 + 16 * k)
    idx[(kind, i)] = (off, ln)


def load(name):
    sid = int(names[name]["id"])
    off, ln = idx[(0, sid)]
    i, kind, psm, nf, clutlen, _ = struct.unpack_from("<IBBHII", data, off)
    p = off + 16
    clut = data[p:p + clutlen]
    p += clutlen
    w, h, stride, _, dl = struct.unpack_from("<HHHHI", data, p)
    pix = data[p + 12:p + 12 + dl]
    return psm, clut, w, h, stride, pix


def expected(psm, clut, w, h, stride, pix):
    n = 16 if psm == 0 else 256
    # undo the CSM1 order for T8 to get the logical palette
    def so(i):
        return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)
    pal = [None] * n
    for pos in range(n):
        e = clut[pos * 4:pos * 4 + 4]
        pal[so(pos) if psm == 1 else pos] = e
    im = Image.new("RGBA", (w, h))
    px = []
    for y in range(h):
        row = pix[y * (stride // 2 if psm == 0 else stride):(y + 1) * (stride // 2 if psm == 0 else stride)]
        for x in range(w):
            v = (row[x // 2] >> (4 * (x & 1))) & 15 if psm == 0 else row[x]
            r, g, b, a = pal[v]
            px.append((r, g, b, min(255, a * 2)))
    im.putdata(px)
    bg = Image.new("RGBA", (w, h), (255, 0, 255, 255))
    return Image.alpha_composite(bg, im)


def carr(name, b):
    return f"static const unsigned char {name}[{len(b)}] __attribute__((aligned(16))) = {{" + \
        ",".join(str(x) for x in b) + "};\n"


with open(f"{out}/verify_data.h", "w") as f:
    for tag, name in (("t4", n4), ("t8", n8)):
        psm, clut, w, h, stride, pix = load(name)
        f.write(f"/* {name} psm={psm} */\n#define {tag.upper()}_W {w}\n#define {tag.upper()}_H {h}\n#define {tag.upper()}_STRIDE {stride}\n")
        f.write(carr(f"{tag}_clut", clut))
        f.write(carr(f"{tag}_pix", pix))
        expected(psm, clut, w, h, stride, pix).save(f"{out}/expected_{tag}.png")
        print(name, "psm", "T4" if psm == 0 else "T8", w, h, "stride", stride, "pix bytes", len(pix))
