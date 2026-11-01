#!/usr/bin/python3
"""gmdis.py <data.win> <code entry name> [--find SUBSTR]   - annotated GML bytecode disassembly (bytecode >= 15)."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from gmcode import decode, OPS, DT, PUSHES

INST = {-1: "self", -2: "other", -3: "all", -4: "noone", -5: "global", -6: "builtin", -7: "local", -9: "stacktop",
        -15: "arg", -16: "static"}
VT = {0x00: "", 0x80: "[stack]", 0xA0: "", 0xE0: "[inst]", 0x10: "[pushaf]", 0x90: "[popaf]"}


def fmt(dw, vars_, funcs, vr, fr, bc, i):
    a = bc + i.addr + 4
    name = OPS[i.op]
    if i.op == 0x45 and i.t1 != 15:
        idx, vt = vr[a]
        it = struct.unpack("<h", struct.pack("<H", i.w & 0xFFFF))[0]
        return f"pop.{DT.get(i.t1, i.t1)}.{DT.get(i.t2, i.t2)} {INST.get(it, it)}.{vars_[idx][0]}{VT.get(vt & 0xF8 if False else vt, '')}"
    if i.op in PUSHES and i.op != 0x84 and i.t1 == 5:
        idx, vt = vr[a]
        it = struct.unpack("<h", struct.pack("<H", i.w & 0xFFFF))[0]
        return f"{name}.v {INST.get(it, it)}.{vars_[idx][0]}{VT.get(vt, '')}"
    if i.op == 0xD9:
        idx, _ = fr[a]
        return f"call.i {funcs[idx][0]} argc={i.w & 0xFFFF}"
    if i.op in PUSHES and i.t1 == 2 and a in fr:
        return f"{name}.i &{funcs[fr[a][0]][0]}"
    if i.op in PUSHES and i.t1 == 6:
        return f"{name}.s \"{dw.string_at(i.arg)[:60]}\""
    if i.op in (0xB6, 0xB7, 0xB8, 0xBA, 0xBB):
        return f"{name} -> {i.addr + i.arg * 4}"
    if i.op == 0x15:
        return f"cmp.{DT.get(i.t1, i.t1)}.{DT.get(i.t2, i.t2)} {i.arg}"
    return f"{name}.{DT.get(i.t1, i.t1)}" + (f".{DT.get(i.t2, i.t2)}" if i.t2 else "") + (f" {i.arg}" if i.arg is not None else "")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    from gmref import build_refs
    dw = DataWin(sys.argv[1])
    ents = dw.code_entries()
    if sys.argv[2] == "--find":
        for n, ln, *_ in ents:
            if sys.argv[3] in n:
                print(n, ln)
        return
    vars_, funcs = dw.variables(), dw.functions()
    vr, fr, _ = build_refs(dw)
    for n, ln, loc, args, bc in ents:
        if n == sys.argv[2]:
            print(f"; {n} len={ln} locals={loc} args={args}")
            for i in decode(dw.buf, bc, ln):
                print(f"{i.addr:6d}  {fmt(dw, vars_, funcs, vr, fr, bc, i)}")
            return
    print("entry not found")


if __name__ == "__main__":
    main()
