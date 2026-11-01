#!/usr/bin/python3
"""packplan.py <game> [area_budget_kb=8192] [resident_pct=30] [layer=code|base|heur]

Streaming-pack planner (asset sharing aware).
  resident : assets used by >= resident_pct % of rooms  -> boot pack, never evicted.
  area     : cluster of rooms (agglomerative on shared non-resident KB along room-graph edges, union <= budget);
             one contiguous pack = union of the area's non-resident assets, each stored once.
  Crossing an area edge = one sequential read of the target area pack (prefetchable); staying inside = no IO.
Writes out/<game>_packs.json and prints disk/IO statistics.
"""
import json
import sys

g = sys.argv[1]
BUDGET = float(sys.argv[2]) if len(sys.argv) > 2 else 8192.0
RES_PCT = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
LAYER = sys.argv[4] if len(sys.argv) > 4 else "code"
RS = json.load(open(f"out/{g}_roomsets.json"))
COST = json.load(open(f"out/{g}_assetcost.json"))
G = json.load(open(f"out/{g}_roomgraph2.json"))


def cost(a):
    return COST[a[0]].get(str(a[1]), 0.0)


rooms = list(RS)
assets = {}
for r, v in RS.items():
    s = {("s", i) for i in v["base"]}
    if LAYER in ("code", "heur"):
        s |= {("s", i) for i in v["code"]}
    if LAYER == "heur":
        s |= {("s", i) for i in v["heur"]}
    s |= {("t", i) for i in v["tilesets"]}
    assets[r] = s
cnt = {}
for s in assets.values():
    for a in s:
        cnt[a] = cnt.get(a, 0) + 1
n = len(rooms)
resident = {a for a, c in cnt.items() if c * 100.0 >= RES_PCT * n}
unique_all = set(cnt)
need = {r: assets[r] - resident for r in rooms}
kb = lambda S: sum(cost(a) for a in S)

# edges between rooms (undirected, weight = shared non-resident KB)
adj = {}
for r, v in G.items():
    if r not in need:
        continue
    for t in set(v["const"]) | set(v["inst"]):
        if t in need and t != r:
            adj.setdefault(r, set()).add(t)
            adj.setdefault(t, set()).add(r)

# agglomerative clustering
area_of = {r: i for i, r in enumerate(rooms)}
members = {i: [r] for i, r in enumerate(rooms)}
union = {i: set(need[r]) for i, r in enumerate(rooms)}


def pair_gain(a, b):
    sh = kb(union[a] & union[b])
    return sh


ukb = {i: kb(u) for i, u in union.items()}
heap_edges = set()
for r, ts in adj.items():
    for t in ts:
        heap_edges.add(tuple(sorted((area_of[r], area_of[t]))))
while True:
    best = None
    for a, b in heap_edges:
        if a == b:
            continue
        if ukb[a] + ukb[b] > 2 * BUDGET:
            continue
        u = kb(union[a] | union[b])
        if u > BUDGET:
            continue
        # gain: shared KB (disk saved vs two packs); tie-break prefers merging small areas
        gain = ukb[a] + ukb[b] - u
        key = (gain, -u)
        if best is None or key > best[0]:
            best = (key, a, b)
    if best is None or best[0][0] <= 0:
        break
    _, a, b = best
    members[a] += members.pop(b)
    union[a] |= union.pop(b)
    ukb[a] = kb(union[a]); ukb.pop(b)
    for r in members[a]:
        area_of[r] = a
    new = set()
    for x, y in heap_edges:
        x = a if x == b else x
        y = a if y == b else y
        if x != y:
            new.add(tuple(sorted((x, y))))
    heap_edges = new

areas = {i: {"rooms": members[i], "kb": round(kb(union[i]), 1), "assets": sorted(f"{k}{j}" for k, j in union[i])}
         for i in members}
# stats
tot_unique = kb(unique_all)
res_kb = kb(resident)
disk_areas = sum(a["kb"] for a in areas.values())
disk_perroom = sum(kb(need[r]) for r in rooms)
edges = [(r, t) for r in G for t in (set(G[r]["const"]) | set(G[r]["inst"])) if r in need and t in need and r != t]
intra = sum(1 for r, t in edges if area_of[r] == area_of[t])
inter = [(r, t) for r, t in edges if area_of[r] != area_of[t]]
load = [kb(union[area_of[t]]) for r, t in inter]
delta = [kb(need[t] - need[r]) for r, t in inter]
avg = lambda v: sum(v) / len(v) if v else 0.0
sizes = sorted(a["kb"] for a in areas.values())
json.dump({"resident": sorted(f"{k}{j}" for k, j in resident), "resident_kb": round(res_kb, 1),
           "areas": areas, "room_area": {r: area_of[r] for r in rooms}}, open(f"out/{g}_packs.json", "w"))
print(f"{g} [{LAYER}] budget {BUDGET/1024:.0f}MB resident>={RES_PCT:.0f}%:")
print(f"  assets unique {len(unique_all)} = {tot_unique/1024:.1f} MB; resident {len(resident)} = {res_kb/1024:.2f} MB")
print(f"  areas {len(areas)} (rooms/area avg {n/len(areas):.1f}); area pack KB min/median/max "
      f"{sizes[0]:.0f}/{sizes[len(sizes)//2]:.0f}/{sizes[-1]:.0f}")
print(f"  disk: area packs {disk_areas/1024:.1f} MB (dup x{disk_areas/max(tot_unique-res_kb,1):.2f}) vs per-room packs "
      f"{disk_perroom/1024:.1f} MB (dup x{disk_perroom/max(tot_unique-res_kb,1):.2f})")
print(f"  graph edges {len(edges)}: intra-area (no IO) {intra} ({100*intra/max(len(edges),1):.0f}%); inter-area {len(inter)}; "
      f"avg area read {avg(load):.0f} KB (full) vs avg delta need {avg(delta):.0f} KB")
