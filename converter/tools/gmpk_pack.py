#!/usr/bin/python3
"""gmpk_pack.py <game> [resident_pct=10] [layer=code|heur] [out=out/<game>.gmpk]

First version of the GMPK packer (docs/pack_format.md). It builds the real *structure* (header, section TOC,
resident boot pack, room index, asset refs, successor lists, music index) from the analysis products:
  out/<g>_roomsets.json  out/<g>_assetcost.json  out/<g>_roomsnd.json  out/<g>_audio.csv  out/<g>_roomgraph2.json
Payload bytes are PLACEHOLDERS (sparse zero) sized from the cost estimates; gs_convert / vag_convert will fill them.
"""
import csv
import json
import struct
import sys

SECT = 2048
g = sys.argv[1]
RES_ARG = sys.argv[2] if len(sys.argv) > 2 else "auto"
import os
EE_BUDGET = float(os.environ.get("GMPK_EE_MB", "6")) * 1048576   # resident textures/tilesets in EE RAM
SFX_BUDGET = float(os.environ.get("GMPK_SFX_KB", "512")) * 1024  # resident SFX in SPU2 RAM
LAYER = sys.argv[3] if len(sys.argv) > 3 else "code"
OUT = sys.argv[4] if len(sys.argv) > 4 else f"out/{g}.gmpk"

RS = json.load(open(f"out/{g}_roomsets.json"))
COST = json.load(open(f"out/{g}_assetcost.json"))
SN = json.load(open(f"out/{g}_roomsnd.json"))
GR = json.load(open(f"out/{g}_roomgraph2.json"))
AUD = list(csv.DictReader(open(f"out/{g}_audio.csv")))
rooms = list(RS)
rid = {r: i for i, r in enumerate(rooms)}

# asset key -> (kind, id, bytes).  kinds: 0 sprite, 1 tileset, 3 sfx
size = {}
for k, ks in (("s", 0), ("t", 1)):
    for i, kb in COST[k].items():
        size[(ks, int(i))] = int(kb * 1024)
for i, row in enumerate(AUD):
    if row["bytes"] != "0" and float(row["secs"]) <= 8.0:
        size[(3, i)] = int(row["vag_22k_mono_bytes"])


def room_assets(r):
    v, s = RS[r], SN.get(r, {"base": [], "code": [], "heur": []})
    out = {(0, i) for i in v["base"]} | {(0, i) for i in v["code"]} | {(1, i) for i in v["tilesets"]}
    out |= {(3, i) for i in s["base"] + s["code"]}
    if LAYER == "heur":
        out |= {(0, i) for i in v["heur"]} | {(3, i) for i in s["heur"]}
    return {a for a in out if size.get(a, 0) > 0}


A = {r: room_assets(r) for r in rooms}
cnt = {}
for s in A.values():
    for a in s:
        cnt[a] = cnt.get(a, 0) + 1
edges = [(r, t) for r in rooms for t in set(GR[r]["const"]) | set(GR[r]["inst"]) if t in A and t != r]


def eval_pct(p):
    res = {a for a, c in cnt.items() if 100.0 * c >= p * len(rooms)}
    ee = sum(size[a] for a in res if a[0] < 3)
    sf = sum(size[a] for a in res if a[0] == 3)
    d = sum(sum(size[a] for a in A[t] - A[r] - res) for r, t in edges) / max(len(edges), 1)
    return res, ee, sf, d


if RES_ARG == "auto":
    # greedy fractional knapsack: benefit(a) = size * (#graph edges that would have to load a), cost = size
    # -> rank by edge_need count, fill the EE and SFX budgets separately
    need_cnt = {}
    for r, t in edges:
        for a in A[t] - A[r]:
            need_cnt[a] = need_cnt.get(a, 0) + 1
    ranked = sorted(need_cnt, key=lambda a: (-need_cnt[a], size[a]))
    res_set, ee, sf = set(), 0, 0
    for a in ranked:
        if need_cnt[a] < 2:
            break
        if a[0] == 3:
            if sf + size[a] <= SFX_BUDGET:
                sf += size[a]; res_set.add(a)
        elif ee + size[a] <= EE_BUDGET:
            ee += size[a]; res_set.add(a)
    d0 = sum(sum(size[a] for a in A[t] - A[r]) for r, t in edges) / max(len(edges), 1)
    d1 = sum(sum(size[a] for a in A[t] - A[r] - res_set) for r, t in edges) / max(len(edges), 1)
    print(f"resident (greedy by transition need, budgets EE {EE_BUDGET/1048576:.1f} MB / SFX {SFX_BUDGET/1024:.0f} KB): "
          f"{len(res_set)} assets, EE {ee/1048576:.2f} MB, SFX {sf/1024:.0f} KB; avg edge delta {d0/1024:.0f} -> {d1/1024:.0f} KB")
    RES_PCT = 0.0
    resident = sorted(res_set)
else:
    RES_PCT = float(RES_ARG)
    resident = sorted(eval_pct(RES_PCT)[0])

RESSET = set(resident)

# room order: BFS over the graph from room 0, then the rest (keeps neighbours close on disc)
order, seen, queue = [], set(), [rooms[0]]
while queue or len(seen) < len(rooms):
    if not queue:
        queue.append(next(r for r in rooms if r not in seen))
    r = queue.pop(0)
    if r in seen:
        continue
    seen.add(r)
    order.append(r)
    for t in sorted(set(GR[r]["const"]) | set(GR[r]["inst"])):
        if t in RS and t not in seen:
            queue.append(t)

pad = lambda n: (n + SECT - 1) // SECT * SECT
# ---- layout ----
res_bytes = sum(size[a] for a in resident)
boot_len = pad(res_bytes + 64 * len(resident))
room_blob = {r: sorted(A[r] - RESSET) for r in rooms}
blob_len = {r: pad(sum(size[a] for a in room_blob[r])) for r in rooms}
# music index: long sounds
music = [(i, int(row["bytes"]), int(row["vag_full_bytes"])) for i, row in enumerate(AUD)
         if row["bytes"] != "0" and float(row["secs"]) > 8.0]

# binary tables
asset_table = b"".join(struct.pack("<IBBHII", i, a[0], 0, 1, size[a], size[a]) for i, a in enumerate(resident)) \
    if False else b""
index_entries, assetrefs, succs = [], [], []
cur = 0  # offsets are filled below once the section start is known
room_off = {}
tmp = 0
for r in order:
    room_off[r] = tmp
    tmp += blob_len[r]
room_data_len = tmp

# ROOM_INDEX = entries(40 B) + assetref array (12 B each) + successor array (8 B each)
ENT = struct.Struct("<IIIIIHHHHII")  # room_id, data_off(sector), data_len(sector), kb_ee, kb_vram, n_assets, n_sfx, n_succ, pad, assets_idx, succ_idx
REF = struct.Struct("<III")
SUC = struct.Struct("<IBBH")
ent_b, ref_b, suc_b = [], [], []
for r in order:
    blob = room_blob[r]
    off = 0
    first_ref = len(ref_b)
    for a in blob:
        ref_b.append(REF.pack((a[0] << 28) | a[1], off, size[a]))
        off += size[a]
    sl = []
    for t in sorted(set(GR[r]["const"])):
        sl.append((t, 0))
    for t in sorted(set(GR[r]["inst"]) - set(GR[r]["const"])):
        sl.append((t, 1))
    # smallest delta first = prefetch priority
    sl = [(t, k) for t, k in sl if t in RS and t != r]
    sl.sort(key=lambda tk: sum(size[a] for a in A[tk[0]] - A[r] - RESSET))
    first_suc = len(suc_b)
    for pr, (t, k) in enumerate(sl):
        suc_b.append(SUC.pack(rid[t], k, min(pr, 255), 0))
    nsfx = sum(1 for a in blob if a[0] == 3)
    ent_b.append(ENT.pack(rid[r], room_off[r] // SECT, blob_len[r] // SECT, sum(size[a] for a in A[r]) // 1024,
                          sum(size[a] for a in A[r] if a[0] < 3) // 1024, len(blob), nsfx, len(sl), 0, first_ref, first_suc))
index_blob = b"".join(ent_b) + b"".join(ref_b) + b"".join(suc_b)
index_len = pad(len(index_blob))
import os as _os
_mp = f"out/{g}_music.csv"
if _os.path.exists(_mp):
    for k, row in enumerate(csv.DictReader(open(_mp))):
        vag = int(float(row["secs"]) * min(int(row["rate"]), 44100) * 16 / 28 * int(row["ch"]))
        music.append((0x80000000 | k, int(row["bytes"]), vag))
music_blob = b"".join(struct.pack("<III", i, ln, vag) for i, ln, vag in music)
music_len = pad(len(music_blob))
music_data_len = pad(sum(v for _, _, v in music))

# sections: BOOT, ROOM_INDEX, ROOM_DATA, MUSIC_INDEX, MUSIC_DATA
secs = []
pos = SECT  # sector 0 = header
for kind, ln, cntv in ((1, boot_len, len(resident)), (2, index_len, len(order)), (3, room_data_len, len(order)),
                       (7, music_len, len(music)), (8, music_data_len, len(music))):
    secs.append((kind, pos // SECT, ln // SECT, cntv))
    pos += ln
total = pos
hdr = struct.pack("<4sIIIIIII", b"GMPK", 1, 0x10, len(secs), int(RES_PCT), 0, 0b100 | 0b1000, 0)  # flags: bit3 placeholder payloads
toc = b"".join(struct.pack("<IIII", *s) for s in secs)
with open(OUT, "wb") as f:
    f.write((hdr + toc).ljust(SECT, b"\0"))
    f.seek(secs[1][1] * SECT)
    f.write(index_blob)
    f.seek(secs[3][1] * SECT)
    f.write(music_blob)
    f.truncate(total)
sizes = [blob_len[r] for r in rooms]
mb = lambda v: v / 1048576
print(f"{g}: gmpk {mb(total):.1f} MB (rooms {len(rooms)}, resident {len(resident)} assets = {mb(res_bytes):.2f} MB, "
      f"room blobs {mb(room_data_len):.1f} MB, music index {len(music)} tracks / {mb(sum(v for _, _, v in music)):.0f} MB ADPCM)")
print(f"  room blob KB: median {sorted(sizes)[len(sizes)//2]//1024}, max {max(sizes)//1024}; successors {len(suc_b)}; index {len(index_blob)//1024} KB")
