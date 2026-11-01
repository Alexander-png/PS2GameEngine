#!/usr/bin/python3
"""gmdiff.py <data.win> <texdir> <pack.gmpk> [room ...]   per room: reference render (full-colour pages) vs pack render
(converted indexed textures). Prints rooms whose pixels differ by more than a small tolerance."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageChops
import gmroom, gmrview
from gmwin import DataWin
from gmitems import room_items, bg_repeats, blit_xf

win, texdir, pack = sys.argv[1:4]
dw = DataWin(win)
names = [dw.string_at(dw.u32(p)) for p in dw.ptr_list("ROOM")]
want = sys.argv[4:] or names
objs, sprs, tp, bgs = dw.objects(), dw.sprites(), dw.tpag_entries(), dw.backgrounds()
pages = {}


def crop(t):
    n = t[10]
    if n not in pages:
        if len(pages) > 4:
            pages.pop(next(iter(pages)))
        pages[n] = Image.open(os.path.join(texdir, f"texture_{n:03d}.png")).convert("RGBA")
    return pages[n].crop((t[0], t[1], t[0] + t[2], t[1] + t[3]))


def ref(name):
    from gmitems import room_model, expand_grid
    m = room_model(dw, names.index(name))
    c = m["color"]
    img = Image.new("RGBA", (m["w"], m["h"]), (c & 255, (c >> 8) & 255, (c >> 16) & 255, 255))
    for it0 in m["items"]:
        for it in (expand_grid(it0) if it0["kind"] == "grid" else [it0]):
            k, i = it["key"][:2]
            t = tp.get(bgs[i]["tpag"]) if k == 1 else tp.get(sprs[i]["frames"][it["key"][2]])
            if not t:
                continue
            sub = crop(t).crop((it["sx"], it["sy"], it["sx"] + it["w"], it["sy"] + it["h"]))
            for (x, y) in (bg_repeats(it, m["w"], m["h"]) if it["kind"] == "bg" else [(it["ax"], it["ay"])]):
                img = blit_xf(img, sub, it, x, y)
    return img


if not sys.argv[4:]:
    inpack = set(gmrview.room_names(pack))
    want = [n for n in want if n in inpack]
bad = 0
for n in want:
    a = ref(n).convert("RGB")
    b = gmrview.render(gmrview.read_room(pack, n)).convert("RGB")
    d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 24 else 0)
    cnt = sum(1 for v in d.getdata() if v)
    frac = cnt / (a.width * a.height)
    if frac > 0.0005:
        bad += 1
        print(f"DIFF {n}: {cnt} px ({frac*100:.2f}%)", flush=True)
print(f"{len(want)} rooms compared, {bad} differ", flush=True)
