#!/usr/bin/python3
"""spritereport.py <data.win> <texdir> <out.csv> [--dump NAME]

Per sprite: frames, size, pages used, max distinct RGBA colors / alpha levels over its frames
(measured from the already-extracted page PNGs, one page at a time) and a palette class
(<=16 -> CLUT4, <=256 -> CLUT8, else FULL). --dump NAME writes frames of that sprite as PNG to out/sprite_dump/.
"""
import os
import sys
import csv
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gmwin import DataWin
from PIL import Image


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return
    win, texdir, outcsv = sys.argv[1:4]
    dump = sys.argv[5] if len(sys.argv) > 5 and sys.argv[4] == "--dump" else None
    dw = DataWin(win)
    sprites = dw.sprites()
    tp = dw.tpag_entries()
    bad = 0
    by_page = defaultdict(list)  # page -> [(sprite idx, frame idx, tpag)]
    for si, s in enumerate(sprites):
        for fi, fp in enumerate(s["frames"]):
            t = tp.get(fp)
            if t is None:
                bad += 1
                continue
            by_page[t[10]].append((si, fi, t))
    stat = [dict(colors=set(), alphas=set(), pages=set(), maxw=0, maxh=0) for _ in sprites]
    for page, items in sorted(by_page.items()):
        path = os.path.join(texdir, f"texture_{page:03d}.png")
        if not os.path.exists(path):
            print("missing page", page)
            continue
        img = Image.open(path).convert("RGBA")
        for si, fi, t in items:
            sx, sy, sw, sh = t[0], t[1], t[2], t[3]
            if sw == 0 or sh == 0:
                continue
            crop = img.crop((sx, sy, sx + sw, sy + sh))
            st = stat[si]
            cols = crop.getcolors(maxcolors=sw * sh) or []
            for _, c in cols:
                if c[3] == 0:
                    continue  # fully transparent pixels: colour is irrelevant
                st["colors"].add(c)
                st["alphas"].add(c[3])
            st["pages"].add(page)
            st["maxw"] = max(st["maxw"], sw)
            st["maxh"] = max(st["maxh"], sh)
            if dump and sprites[si]["name"] == dump:
                os.makedirs("out/sprite_dump", exist_ok=True)
                crop.save(f"out/sprite_dump/{dump}_{fi}.png")
        img.close()
    cls = defaultdict(int)
    with open(outcsv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["idx", "name", "frames", "w", "h", "pages", "colors", "alpha_levels", "class"])
        for si, s in enumerate(sprites):
            st = stat[si]
            n = len(st["colors"])
            # colour entries with alpha: palette must hold each distinct RGBA
            c = "EMPTY" if n == 0 else "CLUT4" if n <= 16 else "CLUT8" if n <= 256 else "FULL"
            cls[c] += 1
            w.writerow([si, s["name"], len(s["frames"]), s["w"], s["h"], len(st["pages"]), n, len(st["alphas"]), c])
    print(f"sprites {len(sprites)}  tpag {len(tp)}  bad frame ptrs {bad}  pages used {len(by_page)}")
    print("classes:", dict(cls))
    print("sprites with partial alpha (>2 alpha levels):",
          sum(1 for st in stat if len(st["alphas"]) > 2))


if __name__ == "__main__":
    main()
