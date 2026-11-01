#!/usr/bin/python3
"""cfgtrace.py <data.win> <entry> [from] [to] - disassembly with abstract stack (depth + known consts) per instruction."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gmcfg
from gmwin import DataWin
from gmref import build_refs
from gmdis import fmt
dw = DataWin(sys.argv[1]); gmcfg.V2 = dw.bytecode_version >= 16
lo = int(sys.argv[3]) if len(sys.argv) > 3 else 0
hi = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9
vars_, funcs = dw.variables(), dw.functions()
vr, fr, _ = build_refs(dw)
for n, ln, loc, args, bc in dw.code_entries():
    if n == sys.argv[2]:
        print(gmcfg.analyze(dw.buf, bc, ln)[:2])
        ins, state = gmcfg.LAST
        for k, i in enumerate(ins):
            if lo <= i.addr <= hi:
                st = state.get(k)
                print(f"{i.addr:6d} d={len(st) if st is not None else '-':>3} top={[c for _, c in st[-3:]] if st else ''}  {fmt(dw, vars_, funcs, vr, fr, bc, i)}")
