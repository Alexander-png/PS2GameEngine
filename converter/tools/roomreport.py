#!/usr/bin/python3
"""roomreport.py <data.win> <sprites.csv> <out.csv> [budget_kb]

Static per-room texture working set (lower bound: sprites reached through instances' objects (+parents),
room layers/tiles and tilesets; sprite_index changes made by GML code are NOT included yet).
VRAM estimate per sprite: all frames x texels x bytes by palette class
(CLUT4 0.5 B, CLUT8 1 B, FULL 2 B (16-bit) ) + palette (64 B / 1 KB). Tilesets counted as CLUT8 (not measured yet).
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gmwin import DataWin

BPT = {"CLUT4": 0.5, "CLUT8": 1.0, "FULL": 2.0, "EMPTY": 0.0}
PAL = {"CLUT4": 64, "CLUT8": 1024, "FULL": 0, "EMPTY": 0}


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return
    win, spcsv, out = sys.argv[1:4]
    budget = float(sys.argv[4]) if len(sys.argv) > 4 else 2048.0
    dw = DataWin(win)
    sprites, tp, objs, rooms, bgs = dw.sprites(), dw.tpag_entries(), dw.objects(), dw.rooms(), dw.backgrounds()
    cls = {}
    with open(spcsv) as f:
        for row in csv.DictReader(f):
            cls[int(row["idx"])] = row["class"]

    def sprite_cost(si):
        s = sprites[si]
        texels = 0
        for fp in s["frames"]:
            t = tp.get(fp)
            if t:
                texels += t[2] * t[3]
        c = cls.get(si, "CLUT8")
        return texels * BPT[c] + PAL[c], texels

    def tileset_cost(bi):
        t = tp.get(bgs[bi]["tpag"]) if 0 <= bi < len(bgs) else None
        return (t[2] * t[3] * 1.0 + 1024) if t else 0.0

    # objects -> sprite ids (own sprite, mask, and ancestors' sprites are reached when instance exists)
    def obj_sprites(oi, seen=None):
        res = set()
        while 0 <= oi < len(objs) and (seen is None or oi not in seen):
            seen = (seen or set()) | {oi}
            o = objs[oi]
            if o["sprite"] >= 0:
                res.add(o["sprite"])
            if o["mask"] >= 0:
                res.add(o["mask"])
            oi = o["parent"]  # parent sprites are not drawn by the child; included only as conservative mask source
            break
        return res

    rows = []
    for r in rooms:
        used = set(r["sprites"])
        for oi in r["objects"]:
            used |= obj_sprites(oi)
        spr_bytes = sum(sprite_cost(s)[0] for s in used)
        ts_bytes = sum(tileset_cost(b) for b in set(r["tilesets"]) | set(r["bgs"]))
        full = sum(1 for s in used if cls.get(s) == "FULL")
        rows.append((r["name"], len(r["objects"]), len(used), len(r["tilesets"]), full,
                     round(spr_bytes / 1024, 1), round(ts_bytes / 1024, 1), round((spr_bytes + ts_bytes) / 1024, 1)))
    rows.sort(key=lambda x: -x[7])
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["room", "instances", "sprites", "tilesets", "full_sprites", "sprite_kb", "tileset_kb", "total_kb"])
        w.writerows(rows)
    tot = [x[7] for x in rows]
    print(f"rooms {len(rows)}  max {max(tot):.0f} KB  median {sorted(tot)[len(tot)//2]:.0f} KB  "
          f"over {budget:.0f} KB budget: {sum(1 for t in tot if t > budget)}")
    print("top 5:", [(x[0], x[7]) for x in rows[:5]])
    print("all-sprites total KB (whole game):",
          round(sum(sprite_cost(i)[0] for i in range(len(sprites))) / 1024))


if __name__ == "__main__":
    main()
