#!/usr/bin/python3
"""gmroomgraph2.py <data.win> [out.json]

Room-transition graph v2 (for speculative loading). Combines
  A. constant room_goto targets of objects whose events/scripts call room_goto(<const>)       (kind "const")
  B. room_goto(self.V) sites + per-instance creation code that assigns V = <room const/name>   (kind "inst")
  C. room_goto_next / room_goto_previous / room_restart                                        (flags)
and reports per room the targets, with the evidence kind, plus coverage statistics.
"""
import json
import os
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from gmcode import decode, PUSHES
from gmref import build_refs

GOTO_FUNCS = {"room_goto": "goto", "room_goto_next": "next", "room_goto_previous": "prev", "room_restart": "restart"}
INST = {-1: "self", -2: "other", -5: "global", -7: "local", -15: "arg"}


def itype_of(i):
    return struct.unpack("<h", struct.pack("<H", i.w & 0xFFFF))[0]


def const_before(ins, k):
    """Constant pushed by the instruction(s) just before ins[k] (skipping conv). Returns ('int',v)/('str',idx)/None."""
    j = k - 1
    while j >= 0 and ins[j].op == 0x07:
        j -= 1
    if j < 0:
        return None
    p = ins[j]
    if p.op == 0x84 and p.arg is not None:
        return ("int", int(p.arg))
    if p.op == 0xC0 and p.t1 in (0, 2, 3) and p.arg is not None:
        return ("int", int(p.arg))
    if p.op == 0xC0 and p.t1 == 6:
        return ("str", p.arg)
    return None


def scan(dw, vr, fr, funcs, vars_, ln, bc):
    """Returns (called, gotos, assigns). gotos: list of (kind, arg) with arg = ('int',v) | ('var','self.V') | None;
    assigns: list of (varname, const) for `self.V = const` pops."""
    ins = decode(dw.buf, bc, ln)
    called, gotos, assigns, copies = set(), [], [], []
    for k, i in enumerate(ins):
        if i.op == 0xD9:
            fn = funcs[fr[bc + i.addr + 4][0]][0]
            called.add(fn)
            kind = GOTO_FUNCS.get(fn)
            if not kind:
                continue
            if kind != "goto":
                gotos.append((kind, None))
                continue
            j = k - 1
            while j >= 0 and ins[j].op == 0x07:
                j -= 1
            arg = None
            if j >= 0:
                p = ins[j]
                if p.op == 0x84 or (p.op == 0xC0 and p.t1 in (0, 2, 3)):
                    arg = ("int", int(p.arg)) if p.arg is not None else None
                elif p.op in PUSHES and p.t1 == 5:
                    idx, vt = vr[bc + p.addr + 4]
                    arg = ("var", f"{INST.get(itype_of(p), itype_of(p))}.{vars_[idx][0]}")
            guard = None
            if arg and arg[0] == "int":
                for m in range(k - 1, 2, -1):
                    b = ins[m]
                    if b.op not in (0x9D, 0xB8):
                        continue
                    v = b.w & 0x7FFFFF
                    if v & 0x400000:
                        v -= 1 << 23
                    if b.addr + 4 * v <= i.addr:
                        continue
                    c, kv, pv = ins[m - 1], ins[m - 2], ins[m - 3]
                    if c.op == 0x15 and ((c.w >> 8) & 0xFF) == 3 and pv.op in PUSHES and pv.t1 == 5 \
                            and (kv.op == 0x84 or kv.op == 0xC0) and kv.arg is not None:
                        sidx, _ = vr[bc + pv.addr + 4]
                        if vars_[sidx][0] == "room":
                            guard = int(kv.arg)
                            break
            gotos.append(("goto", arg) if guard is None else ("goto", arg, guard))
        elif i.op == 0x45 and i.t1 != 15:
            idx, vt = vr[bc + i.addr + 4]
            if itype_of(i) == -1 and vt == 0xA0:
                c = const_before(ins, k)
                if c:
                    assigns.append((vars_[idx][0], c))
            # copy: push.v ?.W ; (conv) ; pop ?.V   -> W flows into V
            # value producer: directly before (normal pop) or three producers back ([value, instance, -9] layout)
            skip = 2 if vt == 0x80 else 0
            j = k - 1
            while j >= 0:
                if ins[j].op == 0x07:
                    j -= 1
                    continue
                if skip == 0:
                    break
                skip -= 1
                j -= 1
            if j >= 0 and ins[j].op in PUSHES and ins[j].op != 0x84 and ins[j].t1 == 5:
                sidx, _ = vr[bc + ins[j].addr + 4]
                copies.append((vars_[sidx][0], vars_[idx][0]))
    return called, gotos, assigns, copies


def room_instances(dw, ri, room_ptrs):
    """[(object id, creation code id)] for room ri (GameObject: x,y,obj,instId,creationCode...)."""
    p = room_ptrs[ri]
    objs_p = dw.u32(p + 48)
    return [(dw.i32(q + 8), dw.i32(q + 16)) for q in dw._plist(objs_p)]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    dw = DataWin(sys.argv[1])
    vr, fr, _ = build_refs(dw)
    funcs, vars_ = dw.functions(), dw.variables()
    ents = dw.code_entries()
    rooms, objs, evs = dw.rooms(), dw.objects(), dw.object_events()
    room_ptrs = dw.ptr_list("ROOM")
    room_by_name = {r["name"]: n for n, r in enumerate(rooms)}
    name_to_idx = {e[0]: n for n, e in enumerate(ents)}
    strs = dw.strings() if hasattr(dw, "strings") else None
    info = {n: scan(dw, vr, fr, funcs, vars_, e[1], e[4]) for n, e in enumerate(ents) if e[1]}
    memo = {}

    def effects(n, stack=()):
        if n in memo:
            return memo[n]
        if n in stack or n not in info:
            return []
        called, gotos = info[n][0], info[n][1]
        res = list(gotos)
        for fn in called:
            m = name_to_idx.get(fn)
            if m is not None and m != n and fn.startswith("gml_Script_"):
                res += effects(m, stack + (n,))
        memo[n] = res
        return res

    obj_eff = []
    for o, es in zip(objs, evs):
        eff = []
        for t, sub, cid, who in es:
            if 0 <= cid < len(ents):
                eff += effects(cid)
        obj_eff.append(eff)

    STRS = dw.strings()

    def str_at(idx):
        return STRS[idx] if isinstance(idx, int) and 0 <= idx < len(STRS) else None

    # variable names that carry a room id: goto argument vars + everything copied into them (to a fixed point)
    room_vars = set()
    for n_, inf in info.items():
        for kind, arg, *_g in inf[1]:
            if kind == "goto" and arg and arg[0] == "var":
                room_vars.add(arg[1].split(".", 1)[1])
    copies_all = [c for inf in info.values() for c in inf[3]]
    changed = True
    while changed:
        changed = False
        for src, dst in copies_all:
            if dst in room_vars and src not in room_vars:
                room_vars.add(src)
                changed = True
    print(f"room-carrying variable names: {len(room_vars)}", sorted(room_vars)[:12])
    graph = {}
    n_inst_edges = n_inst_sites = 0
    for ri, r in enumerate(rooms):
        out = {"const": set(), "inst": set(), "next": False, "prev": False, "restart": False, "dyn": False}
        for oi, cc in room_instances(dw, ri, room_ptrs):
            if not (0 <= oi < len(objs)):
                continue
            assigns = {}
            if 0 <= cc < len(ents) and cc in info:
                for var, c in info[cc][2]:
                    assigns[var] = c
            for var, c in assigns.items():
                if c[0] == "str":
                    # strings naming rooms: "roomA to roomB", "to_roomB", plain "roomB" -> every named room != this one
                    sv = str_at(c[1]) or ""
                    for tok in set(re.split(r"[^A-Za-z0-9_]+", sv.replace("to_", " ", 1) if sv.startswith("to_") else sv)):
                        t = room_by_name.get(tok)
                        if t is not None and t != ri:
                            out["inst"].add(t)
                            n_inst_edges += 1
                elif var in room_vars:
                    t = c[1]
                    if 0 <= t < len(rooms):
                        out["inst"].add(t)
                        n_inst_edges += 1
            for kind, arg, *gd in obj_eff[oi]:
                if gd and gd[0] != ri:
                    continue
                if kind in ("next", "prev", "restart"):
                    out[kind] = True
                    continue
                if arg is None:
                    out["dyn"] = True
                elif arg[0] == "int":
                    if 0 <= arg[1] < len(rooms):
                        out["const"].add(arg[1])
                elif arg[0] == "var" and arg[1].startswith("self."):
                    n_inst_sites += 1
                    c = assigns.get(arg[1][5:])
                    if c is not None:
                        t = c[1] if c[0] == "int" else room_by_name.get(str_at(c[1]) or "")
                        if t is not None and 0 <= t < len(rooms):
                            out["inst"].add(t)
                            n_inst_edges += 1
                    else:
                        out["dyn"] = True
                else:
                    out["dyn"] = True
        graph[ri] = out
    nr = len(rooms)
    has_inst = sum(1 for g in graph.values() if g["inst"])
    has_any = sum(1 for g in graph.values() if g["const"] or g["inst"] or g["next"] or g["prev"])
    avg_i = sum(len(g["inst"]) for g in graph.values()) / nr
    print(f"rooms {nr}: instance-variable goto sites resolved {n_inst_edges}/{n_inst_sites}; rooms with instance-targets "
          f"{has_inst} (avg {avg_i:.2f}); rooms with any static edge {has_any}")
    if len(sys.argv) > 2:
        names = [r["name"] for r in rooms]
        json.dump({names[ri]: {"const": sorted(names[c] for c in g["const"]), "inst": sorted(names[c] for c in g["inst"]),
                               "next": g["next"], "prev": g["prev"], "dyn": g["dyn"]} for ri, g in graph.items()},
                  open(sys.argv[2], "w"), indent=1)


if __name__ == "__main__":
    main()
