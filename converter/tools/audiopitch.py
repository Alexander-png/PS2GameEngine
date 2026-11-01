#!/usr/bin/python3
"""audiopitch.py <data.win> [substr=pitch] : list calls to functions whose name contains substr, with preceding 8 instrs."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from gmcode import decode
from gmref import build_refs
from gmdis import fmt
dw = DataWin(sys.argv[1]); sub = sys.argv[2] if len(sys.argv) > 2 else "pitch"
vars_, funcs = dw.variables(), dw.functions()
vr, fr, _ = build_refs(dw)
for n, ln, loc, args, bc in dw.code_entries():
    ins = decode(dw.buf, bc, ln)
    for k, i in enumerate(ins):
        if i.op == 0xD9:
            a = bc + i.addr + 4
            if a in fr and sub in funcs[fr[a][0]][0]:
                print(f"== {n} @{i.addr} {funcs[fr[a][0]][0]}")
                for j in ins[max(0, k - 8):k + 1]:
                    print("   ", fmt(dw, vars_, funcs, vr, fr, bc, j))
