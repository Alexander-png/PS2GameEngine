"""residsweep.py <game> : resident-threshold sweep. Per graph edge A->B: delta=kb(B - A - resident), full=kb(B - resident)."""
import json, sys
g = sys.argv[1]
RS = json.load(open(f"out/{g}_roomsets.json")); C = json.load(open(f"out/{g}_assetcost.json")); G = json.load(open(f"out/{g}_roomgraph2.json"))
kb = lambda S: sum(C[k].get(str(i), 0) for k, i in S)
A = {r: {("s", i) for i in v["base"] + v["code"]} | {("t", i) for i in v["tilesets"]} for r, v in RS.items()}
cnt = {}
for s in A.values():
    for a in s: cnt[a] = cnt.get(a, 0) + 1
n = len(A)
E = [(r, t) for r in G for t in set(G[r]["const"]) | set(G[r]["inst"]) if r in A and t in A and r != t]
print(g, "edges", len(E), "rooms", n)
for pct in (50, 30, 20, 10, 5):
    res = {a for a, c in cnt.items() if 100 * c >= pct * n}
    d = [kb(A[t] - A[r] - res) for r, t in E]; f = [kb(A[t] - res) for r, t in E]
    d.sort(); mx = max((kb(A[r] - res) for r in A))
    print(f"  resident>={pct:2d}%: {kb(res)/1024:5.2f} MB ({len(res)} assets) | delta KB avg {sum(d)/len(d):6.0f} p90 {d[int(.9*len(d))]:6.0f} max {d[-1]:6.0f}"
          f" | full-room avg {sum(f)/len(f):6.0f} | worst room set {mx/1024:.1f} MB")
