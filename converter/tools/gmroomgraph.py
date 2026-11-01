#!/usr/bin/python3
"""gmroomgraph.py <data.win> [out.json]

Static room-transition graph for speculative resource loading.
 - finds every call to room_goto / room_goto_next / room_goto_previous / room_restart in the bytecode;
 - resolves constant room ids pushed as the argument (pushi/push.i/push.d just before the call);
 - attributes call sites to objects (via event -> code entry, following script calls transitively) and to rooms
   (rooms that contain instances of those objects, plus room creation code);
 - emits per room: constant targets, uses next/previous, dynamic (unresolved) transition flag.
"""
import json
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin, EVENT_NAMES
from gmcode import decode, PUSHES
from gmref import build_refs

GOTO_FUNCS = {"room_goto": "goto", "room_goto_next": "next", "room_goto_previous": "prev", "room_restart": "restart"}


def scan_entry(dw, vr, fr, funcs, name, ln, bc):
    """Returns (calls, targets) where calls = set of called function names (scripts), targets list of
    (kind, const_room or None)."""
    ins = decode(dw.buf, bc, ln)
    called = set()
    gotos = []
    for k, i in enumerate(ins):
        if i.op != 0xD9:
            continue
        fn = funcs[fr[bc + i.addr + 4][0]][0]
        called.add(fn)
        kind = GOTO_FUNCS.get(fn)
        if not kind:
            continue
        const = None
        if kind == "goto":
            j = k - 1
            while j >= 0 and ins[j].op == 0x07:  # skip conv
                j -= 1
            if j >= 0 and ((ins[j].op == 0x84) or (ins[j].op == 0xC0 and ins[j].t1 in (0, 2, 3))) and ins[j].arg is not None:
                const = int(ins[j].arg)
        gotos.append((kind, const))
    return called, gotos


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    dw = DataWin(sys.argv[1])
    vr, fr, _ = build_refs(dw)
    funcs = dw.functions()
    ents = dw.code_entries()
    rooms = dw.rooms()
    objs = dw.objects()
    evs = dw.object_events()
    name_to_idx = {e[0]: n for n, e in enumerate(ents)}
    info = {}
    for n, (name, ln, loc, args, bc) in enumerate(ents):
        if ln:
            info[n] = scan_entry(dw, vr, fr, funcs, name, ln, bc)
    # transitive goto effects through script calls
    memo = {}

    def effects(n, stack=()):
        if n in memo:
            return memo[n]
        if n in stack or n not in info:
            return []
        called, gotos = info[n]
        res = list(gotos)
        for fn in called:
            m = name_to_idx.get(fn)
            if m is not None and m != n and fn.startswith("gml_Script_"):
                res += effects(m, stack + (n,))
        memo[n] = res
        return res

    obj_effects = []
    for o, es in zip(objs, evs):
        eff = []
        for t, sub, cid, who in es:
            if 0 <= cid < len(ents):
                eff += effects(cid)
        obj_effects.append(eff)
    room_names = [r["name"] for r in rooms]
    freq = defaultdict(int)  # number of rooms that contain each object
    for r in rooms:
        for oi in set(r["objects"]):
            freq[oi] += 1
    graph = {}
    n_const = n_dyn = 0
    for ri, r in enumerate(rooms):
        out = {"const": set(), "local": set(), "next": False, "prev": False, "restart": False, "dynamic": False}
        eff = []
        for oi in set(r["objects"]):
            if 0 <= oi < len(objs):
                eff += obj_effects[oi]
                if freq[oi] <= 8:  # room-specific object (doors, triggers, cutscenes)
                    for kind, const in obj_effects[oi]:
                        if kind == "goto" and const is not None and 0 <= const < len(rooms):
                            out["local"].add(const)
        # room creation code
        for suffix in (f"gml_Room_{r['name']}_Create",):
            m = name_to_idx.get(suffix)
            if m is not None:
                eff += effects(m)
        for kind, const in eff:
            if kind == "goto":
                if const is not None and 0 <= const < len(rooms):
                    out["const"].add(const)
                    n_const += 1
                else:
                    out["dynamic"] = True
                    n_dyn += 1
            else:
                out[kind] = True
        graph[ri] = out
    # persistent objects (present in any room via instance_create) are not attributed here: reported separately
    total_sites = sum(len(g) for _, g in info.values())
    sites_const = sum(1 for _, g in info.values() for k, c in g if k == "goto" and c is not None)
    sites_dyn = sum(1 for _, g in info.values() for k, c in g if k == "goto" and c is None)
    sites_other = total_sites - sites_const - sites_dyn
    print(f"rooms {len(rooms)}  goto-call sites {total_sites}: constant {sites_const}, dynamic {sites_dyn}, "
          f"next/prev/restart {sites_other}")
    has_const = sum(1 for g in graph.values() if g["const"])
    has_dyn = sum(1 for g in graph.values() if g["dynamic"])
    has_np = sum(1 for g in graph.values() if g["next"] or g["prev"])
    none = sum(1 for g in graph.values() if not (g["const"] or g["dynamic"] or g["next"] or g["prev"]))
    avg = sum(len(g["const"]) for g in graph.values()) / max(1, len(graph))
    avgl = sum(len(g["local"]) for g in graph.values()) / max(1, len(graph))
    has_local = sum(1 for g in graph.values() if g["local"])
    big = sorted(((freq[oi], objs[oi]["name"], len({c for k, c in obj_effects[oi] if k == "goto" and c is not None}))
                  for oi in freq if obj_effects[oi]), reverse=True)[:5]
    print(f"room-specific (object in <=8 rooms) constant targets: rooms with any {has_local}, avg out-degree {avgl:.2f}")
    print("most widespread objects with room_goto:", big)
    print(f"rooms with constant targets {has_const}, dynamic {has_dyn}, next/prev {has_np}, no transition found {none}; "
          f"avg constant out-degree {avg:.2f}")
    if len(sys.argv) > 2:
        out = {room_names[ri]: {"const": sorted(room_names[c] for c in g["const"]), "local": sorted(room_names[c] for c in g["local"]), "next": g["next"],
                                "prev": g["prev"], "dynamic": g["dynamic"]} for ri, g in graph.items()}
        json.dump(out, open(sys.argv[2], "w"), indent=1)


if __name__ == "__main__":
    main()
