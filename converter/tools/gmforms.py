#!/usr/bin/python3
"""gmforms.py <data.win>: histogram of instruction forms (what the C emitter must handle)."""
import os, struct, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from gmcode import decode, OPS, DT
from gmref import build_refs
dw = DataWin(sys.argv[1]); vr, fr, _ = build_refs(dw)
H = collections.Counter()
for n, ln, loc, args, bc in dw.code_entries():
    for i in decode(dw.buf, bc, ln):
        o = OPS[i.op]; a = bc + i.addr + 4
        if i.op == 0x45 and i.t1 != 15:
            it = struct.unpack("<h", struct.pack("<H", i.w & 0xFFFF))[0]
            H[f"pop.{DT[i.t1]}.{DT[i.t2]} inst={it if it<0 else 'obj'} vt={vr[a][1]:#x}"] += 1
        elif i.op in (0xC0, 0xC1, 0xC2, 0xC3) and i.t1 == 5:
            it = struct.unpack("<h", struct.pack("<H", i.w & 0xFFFF))[0]
            H[f"{o}.v inst={it if it<0 else 'obj'} vt={vr[a][1]:#x}"] += 1
        elif i.op in (0xC0, 0xC1, 0xC2, 0xC3, 0x84):
            H[f"{o}.{DT.get(i.t1, i.t1)}"] += 1
        elif i.op == 0xFF:
            H[f"break {i.arg} t={i.t1}"] += 1
        elif i.op == 0x86:
            H[f"dup n={i.extra&0x7f} flag={(i.w>>8)&0xff:#x} t={DT.get(i.t1)}"] += 1
        elif i.op in (0x07,) or (0x08 <= i.op <= 0x15):
            H[f"{o}.{DT.get(i.t1, i.t1)}.{DT.get(i.t2, i.t2)}"] += 1
        else:
            H[o] += 1
for k, v in sorted(H.items(), key=lambda kv: -kv[1]): print(f"{v:8d} {k}")
