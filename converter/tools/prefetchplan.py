"""prefetchplan.py <game> [budget_kb] : evaluate speculative-prefetch coverage from roomgraph2 + rooms.csv.
Budget = VRAM/RAM region available for prefetched neighbours (separate from the current room's set).
Writes out/<game>_prefetch.json: per room, successors ranked by size (small first) with 'fit' flags."""
import csv, json, sys
g = sys.argv[1]; B = float(sys.argv[2]) if len(sys.argv) > 2 else 2048
sz = {r["room"]: float(r["total_kb"]) for r in csv.DictReader(open(f"out/{g}_rooms.csv"))}
G = json.load(open(f"out/{g}_roomgraph2.json")); names = list(G)
plan = {}; E = Ef = R = Rall = Rnone = Rdyn = 0
for r, v in G.items():
    succ = set(v["const"]) | set(v["inst"])
    succ.discard(r)
    lst = sorted(succ, key=lambda s: sz.get(s, 0))
    used = 0; items = []
    for s in lst:
        k = sz.get(s, 0); E += 1
        if used + k <= B:
            used += k; Ef += 1; items.append((s, k, True))
        else:
            items.append((s, k, False))
    if lst:
        R += 1
        if all(i[2] for i in items): Rall += 1
    else:
        Rnone += 1
    if v["dyn"]: Rdyn += 1
    plan[r] = {"succ": items, "next": v["next"], "prev": v["prev"], "dyn": v["dyn"], "kb": sz.get(r, 0)}
json.dump(plan, open(f"out/{g}_prefetch.json", "w"), indent=0)
print(f"{g} budget {B/1024:.0f}MB: edges {E}, fully prefetchable {Ef} ({100*Ef/max(E,1):.0f}%); "
      f"rooms with succ {R}, all succ fit {Rall} ({100*Rall/max(R,1):.0f}%); no static succ {Rnone}; has dyn {Rdyn}")
