#!/usr/bin/python3
"""gotoargs.py <data.win> - classify the argument expression of room_goto call sites; list room-related functions used."""
import os, struct, sys
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from gmcode import decode, PUSHES
from gmref import build_refs
dw = DataWin(sys.argv[1])
vr, fr, _ = build_refs(dw)
funcs, vars_ = dw.functions(), dw.variables()
cls = Counter(); varc = Counter(); fcalls = Counter()
INST = {-1: "self", -2: "other", -5: "global", -7: "local", -15: "arg", -9: "stack"}
for n, ln, loc, a, bc in dw.code_entries():
    if not ln:
        continue
    ins = decode(dw.buf, bc, ln)
    for k, i in enumerate(ins):
        if i.op != 0xD9:
            continue
        fn = funcs[fr[bc + i.addr + 4][0]][0]
        if "room" in fn and not fn.startswith("gml_"):
            fcalls[fn] += 1
        if fn != "room_goto":
            continue
        j = k - 1
        while j >= 0 and ins[j].op == 0x07:
            j -= 1
        p = ins[j]
        if p.op in (0x84,) or (p.op == 0xC0 and p.t1 in (0, 2, 3)):
            cls["const"] += 1
        elif p.op in PUSHES and p.t1 == 5:
            idx, vt = vr[bc + p.addr + 4]
            it = struct.unpack("<h", struct.pack("<H", p.w & 0xFFFF))[0]
            key = f"{INST.get(it, it)}.{vars_[idx][0]}"
            cls["variable"] += 1; varc[key] += 1
        elif p.op == 0xD9:
            cls["call result"] += 1; varc["call:" + funcs[fr[bc + p.addr + 4][0]][0]] += 1
        else:
            cls["other"] += 1
print(dict(cls)); print("top variable args:", varc.most_common(8)); print("room functions:", fcalls.most_common(12))
