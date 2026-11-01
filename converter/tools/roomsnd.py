#!/usr/bin/python3
"""roomsets.py <data.win> <sprites.csv> <game> : per-room asset sets INCLUDING code references.

Layers (each kept separately so the numbers can be compared):
  base   : room layers/tiles + sprite/mask of every instance's object            (as roomreport.py)
  code   : + sprite constants found in the code of those objects (events, parents' events, called
           scripts, instance/room creation code):
             - `sprite_index = <const>` / `mask_index = <const>`
             - first argument of draw_sprite*/sprite_* calls, layer_sprite_create(4th)
             - strings equal to a sprite name (asset_get_index("spr_x"))
             - instance_create*(...<const object>) spawns -> that object's sprites + code, recursively
  heur   : + int constants assigned to variables whose name contains 'spr'/'sprite'/'image' and fit the sprite range
Output: out/<game>_roomsets.json  {room: {base:[ids], code:[ids], heur:[ids], tilesets:[ids], objs:[ids]}}
        out/<game>_roomsets.csv   sizes (KB) per layer.
"""
import csv
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from gmcode import decode, PUSHES
from gmref import build_refs
from gmroomgraph2 import const_before, itype_of, room_instances

BPT = {"CLUT4": 0.5, "CLUT8": 1.0, "FULL": 2.0, "EMPTY": 0.0}
PAL = {"CLUT4": 64, "CLUT8": 1024, "FULL": 0, "EMPTY": 0}
SPR_ARG0 = ("gml_Script_snd_", "gml_Script_scr_textsound", "snd_play", "sound_play", "audio_play_sound", "audio_sound_", "audio_stop_sound", "audio_pause_sound", "audio_resume_sound", "audio_is_playing", "audio_play_in_sync_group")
SPAWN = {"instance_create": 2, "instance_create_layer": 3, "instance_create_depth": 3}
SPR_VARS = ()


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return
    win, spcsv, game = sys.argv[1:4]
    dw = DataWin(win)
    vr, fr, _ = build_refs(dw)
    funcs, vars_ = dw.functions(), dw.variables()
    ents = dw.code_entries()
    sprites, tp, objs, rooms, bgs = dw.sprites(), dw.tpag_entries(), dw.objects(), dw.rooms(), dw.backgrounds()
    evs = dw.object_events()
    room_ptrs = dw.ptr_list("ROOM")
    STRS = dw.strings()
    spr_by_name = {s["name"]: i for i, s in enumerate(dw.sounds())}
    obj_by_name = {o["name"]: i for i, o in enumerate(objs)}
    name_to_idx = {e[0]: n for n, e in enumerate(ents)}
    cls = {}
    for row in csv.DictReader(open(spcsv)):
        cls[int(row["idx"])] = row["class"]
    nspr, nobj = len(dw.sounds()), len(objs)

    # ---- per code entry scan ----
    def scan(n):
        e = ents[n]
        bc = e[4]
        ins = decode(dw.buf, bc, e[1])
        code, heur, spawn, calls = set(), set(), set(), set()
        for k, i in enumerate(ins):
            if i.op == 0xC0 and i.t1 == 6 and isinstance(i.arg, int) and 0 <= i.arg < len(STRS):
                s = STRS[i.arg]
                if s in spr_by_name:
                    code.add(spr_by_name[s])
                elif s in obj_by_name:
                    spawn.add(obj_by_name[s])
            elif i.op == 0xD9:
                fn = funcs[fr[bc + i.addr + 4][0]][0]
                calls.add(fn)
                c = const_before(ins, k)
                if fn.startswith(SPR_ARG0) and c and c[0] == "int" and 0 <= c[1] < nspr:
                    code.add(c[1])
                elif fn == "layer_sprite_create":
                    # args (layer,x,y,sprite): sprite pushed first -> hard to locate; take any const sprite in the 4 pushes
                    pass
                elif fn in SPAWN:
                    argc = i.w & 0xFFFF
                    pushes = [x for x in ins[max(0, k - 3 * argc):k] if x.op != 0x07]
                    if len(pushes) >= argc:
                        p = pushes[-argc:][0] if argc else None  # first pushed == last argument == object
                        if SPAWN[fn] == argc - 1 and p is not None and p.op in (0x84, 0xC0) and p.t1 in (0, 2, 3, 15) \
                                and p.arg is not None:
                            o = int(p.arg)
                            if 0 <= o < nobj:
                                spawn.add(o)
            elif i.op == 0x45 and i.t1 != 15:
                idx, vt = vr[bc + i.addr + 4]
                if itype_of(i) == -1 and vt == 0xA0:
                    vn = vars_[idx][0]
                    c = const_before(ins, k)
                    if c and c[0] == "int" and 0 <= c[1] < nspr:
                        if vn in SPR_VARS:
                            code.add(c[1])
                        elif any(t in vn.lower() for t in ("snd", "sound", "sfx", "audio", "voice")):
                            heur.add(c[1])
        return code, heur, spawn, calls

    cache = {}

    def info(n):
        if n not in cache:
            cache[n] = scan(n) if 0 <= n < len(ents) and ents[n][1] else (set(), set(), set(), set())
        return cache[n]

    script_memo = {}

    def script_closure(n, stack=()):
        """(code, heur, spawn) for entry n including called scripts."""
        if n in script_memo:
            return script_memo[n]
        if n in stack:
            return set(), set(), set()
        code, heur, spawn, calls = info(n)
        code, heur, spawn = set(code), set(heur), set(spawn)
        for fn in calls:
            for pre in ("gml_Script_", "gml_GlobalScript_"):
                if fn.startswith("gml_Script_"):
                    m = name_to_idx.get(pre + fn[len("gml_Script_"):])
                    if m is not None and m != n:
                        a, b, c = script_closure(m, stack + (n,))
                        code |= a; heur |= b; spawn |= c
                        break
        script_memo[n] = (code, heur, spawn)
        return script_memo[n]

    obj_memo = {}

    def obj_closure(oi, stack=()):
        """(own sprites, code-derived sprites incl. spawned objects, heuristic sprites) for object oi."""
        if oi in obj_memo:
            return obj_memo[oi]
        if oi in stack or not 0 <= oi < nobj:
            return set(), set(), set()
        own = set()
        code, heur, spawn = set(), set(), set()
        chain, o = [], oi
        while 0 <= o < nobj and o not in chain:
            chain.append(o)
            o = objs[o]["parent"]
        for o in chain:
            for t, sub, cid, who in evs[o]:
                a, b, c = script_closure(cid)
                code |= a; heur |= b; spawn |= c
        for sp in spawn:
            if sp != oi:
                a, b, c = obj_closure(sp, stack + (oi,))
                code |= a | b; heur |= c
        obj_memo[oi] = (own, code, heur)
        return obj_memo[oi]

    # ---- costs: VAG 22.05k mono bytes per sound index (SFX-like <=8 s) ; long sounds are streamed ----
    arows = list(csv.DictReader(open(f"out/{game}_audio.csv")))
    names = [x["name"] for x in dw.sounds()]
    byname = {r["name"]: r for r in arows}
    def sprite_cost(i):
        r = byname.get(names[i])
        if not r or float(r["secs"]) > 8.0:
            return 0.0
        return float(r["vag_22k_mono_bytes"])
    def stream_cost(i):
        r = byname.get(names[i])
        return float(r["bytes"]) if r and float(r["secs"]) > 8.0 else 0.0
    def tileset_cost(b):
        return 0.0
    bgs = []

    out, rows = {}, []
    for ri, r in enumerate(rooms):
        base = set()
        code, heur = set(), set()
        objset = set(r["objects"])
        ccs = [r["creation_code"]]
        for oi, cc in room_instances(dw, ri, room_ptrs):
            ccs.append(cc)
        for cc in ccs:
            if cc >= 0:
                a, b, c = script_closure(cc)
                code |= a; heur |= b
                objset |= c
        placed = set(r["objects"])
        for oi in objset:
            own, c2, h2 = obj_closure(oi)
            if oi in placed:
                base |= own
            else:
                code |= own
            code |= c2
            heur |= h2
        ts = set(r["tilesets"]) | set(r["bgs"])
        base_only = {s for s in base if s >= 0}
        code = {s for s in code if s >= 0} - base_only
        heur = {s for s in heur if s >= 0} - base_only - code
        kb = lambda ss: sum(sprite_cost(s) for s in ss) / 1024
        tkb = sum(tileset_cost(b) for b in ts) / 1024
        out[r["name"]] = {"base": sorted(base_only), "code": sorted(code), "heur": sorted(heur), "tilesets": []}
        rows.append((r["name"], len(base_only), len(code), len(heur), round(kb(base_only) + tkb, 1),
                     round(kb(code), 1), round(kb(heur), 1)))
    json.dump(out, open(f"out/{game}_roomsnd.json", "w"))
    with open(f"out/{game}_roomsnd.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["room", "n_base", "n_code", "n_heur", "base_kb", "code_add_kb", "heur_add_kb"])
        w.writerows(rows)
    n = len(rows)
    tb = [x[4] for x in rows]; tc = [x[4] + x[5] for x in rows]; th = [x[4] + x[5] + x[6] for x in rows]
    med = lambda v: sorted(v)[len(v) // 2]
    print(f"{game}: rooms {n}; median KB base {med(tb):.0f} +code {med(tc):.0f} +heur {med(th):.0f}; "
          f"rooms>2MB base {sum(t > 2048 for t in tb)} +code {sum(t > 2048 for t in tc)} +heur {sum(t > 2048 for t in th)}; "
          f"rooms gaining code sprites {sum(1 for x in rows if x[2])}")


if __name__ == "__main__":
    main()
