#!/usr/bin/python3
"""gmpk_sim.py <file.gmpk> [cache_mb=12] [prefetch_mb=3] [walks=300] [steps=120]

Parses a .gmpk written by gmpk_pack.py (validates the format round-trip) and replays random walks over the
successor graph with the loading policy of docs/pack_format.md section 5/6:
  - EE cache with LRU eviction (assets of the current room pinned)
  - prefetch of successors (smallest delta first) into `prefetch_mb` while the player stays in a room
  - on room enter: delta rule (>=50% missing -> one whole-blob read, else merged per-asset reads, gap <= 64 KB)
Reports synchronous (non-prefetched) bytes/reads per transition and estimated stalls on DVD / HDD (assumed speeds).
"""
import random
import struct
import sys
from collections import OrderedDict

SECT = 2048
path = sys.argv[1]
CACHE = float(sys.argv[2]) * 1048576 if len(sys.argv) > 2 else 12 * 1048576
PREF = float(sys.argv[3]) * 1048576 if len(sys.argv) > 3 else 3 * 1048576
WALKS = int(sys.argv[4]) if len(sys.argv) > 4 else 300
STEPS = int(sys.argv[5]) if len(sys.argv) > 5 else 120
MEDIA = {"DVD": (6e6, 0.15), "HDD": (20e6, 0.01)}  # assumed bytes/s, seek s

f = open(path, "rb")
hd = f.read(SECT)
magic, ver, toc_off, nsec, _, _, flags, _ = struct.unpack_from("<4sIIIIIII", hd)
assert magic == b"GMPK" and ver == 1, "bad header"
secs = {k: (o, l, c) for k, o, l, c in (struct.unpack_from("<IIII", hd, toc_off + 16 * i) for i in range(nsec))}
o, l, nrooms = secs[2]
f.seek(o * SECT)
idx = f.read(l * SECT)
ENT = struct.Struct("<IIIIIHHHHII"); REF = struct.Struct("<III"); SUC = struct.Struct("<IBBH")
ents = [ENT.unpack_from(idx, i * ENT.size) for i in range(nrooms)]
refs_base = nrooms * ENT.size
n_refs = sum(e[5] for e in ents)
suc_base = refs_base + n_refs * REF.size
rooms = {}
for e in ents:
    rid, doff, dlen, kbe, kbv, na, nsfx, nsucc, _, ai, si = e
    refs = [REF.unpack_from(idx, refs_base + (ai + k) * REF.size) for k in range(na)]
    succ = [SUC.unpack_from(idx, suc_base + (si + k) * SUC.size) for k in range(nsucc)]
    rooms[rid] = dict(refs=[(r[0], r[1], r[2]) for r in refs], succ=[s[0] for s in succ], blob=dlen * SECT)
print(f"parsed {nrooms} rooms, {n_refs} asset refs from {path} (format OK)")


def assets(rid):
    return {r[0]: r[2] for r in rooms[rid]["refs"]}


def plan_reads(rid, have):
    """Return (bytes, reads) to fetch the missing assets of room rid, using the delta rule."""
    refs = [r for r in rooms[rid]["refs"] if r[0] not in have]
    if not refs:
        return 0, 0
    miss = sum(r[2] for r in refs)
    if miss * 2 >= max(rooms[rid]["blob"], 1):
        return rooms[rid]["blob"], 1
    refs.sort(key=lambda r: r[1])
    reads, byts, end = 0, 0, None
    for _, off, ln in refs:
        if end is not None and off - end <= 65536:
            byts += (off + ln) - end
            end = off + ln
        else:
            reads += 1
            byts += ln
            end = off + ln
    return byts, reads


def run(prefetch):
    random.seed(1)
    cache = OrderedDict()  # asset -> size
    sync_b, sync_r, trans = [], [], 0
    ids = list(rooms)
    for _ in range(WALKS):
        cache.clear()
        cur = random.choice(ids)
        for _ in range(STEPS):
            nxt = rooms[cur]["succ"]
            if not nxt:
                nxt = [random.choice(ids)]  # unknown exit: cold jump
            cur2 = random.choice(nxt)
            # prefetch during stay in `cur`
            if prefetch:
                budget = PREF
                for t in sorted(rooms[cur]["succ"], key=lambda t: sum(s for a, s in assets(t).items() if a not in cache)):
                    need = {a: s for a, s in assets(t).items() if a not in cache}
                    sz = sum(need.values())
                    if sz <= budget:
                        budget -= sz
                        for a, s in need.items():
                            cache[a] = s
            b, r = plan_reads(cur2, cache)
            sync_b.append(b)
            sync_r.append(r)
            for a, s in assets(cur2).items():
                cache[a] = s
                cache.move_to_end(a)
            cur = cur2
            pinned = set(assets(cur))
            total = sum(cache.values())
            for a in list(cache):
                if total <= CACHE:
                    break
                if a not in pinned:
                    total -= cache.pop(a)
    return sync_b, sync_r


def stats(v):
    v = sorted(v)
    return sum(v) / len(v), v[int(.95 * len(v))], v[-1]


for pf in (False, True):
    b, r = run(pf)
    ab, pb, mb_ = stats(b)
    zero = sum(1 for x in b if x == 0) / len(b)
    print(f"prefetch={'on ' if pf else 'off'}: sync load avg {ab/1024:7.0f} KB p95 {pb/1024:7.0f} KB max {mb_/1024:7.0f} KB; "
          f"transitions with no sync IO {100*zero:4.0f}%; avg reads {sum(r)/len(r):.2f}")
    for name, (bw, seek) in MEDIA.items():
        st = sorted(x / bw + (seek * rr) for x, rr in zip(b, r))
        print(f"    {name}: stall avg {1000*sum(st)/len(st):5.0f} ms  p95 {1000*st[int(.95*len(st))]:5.0f} ms  max {1000*st[-1]:5.0f} ms")
