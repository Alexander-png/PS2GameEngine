#!/usr/bin/python3
"""gmref.py - resolve variable/function operand words of GML bytecode (>=15) through the VARI/FUNC reference chains.

Each VARI/FUNC entry holds (occurrences, firstAddress). The operand word of the instruction at an occurrence has
bits 26..0 = byte offset (operand-to-operand) to the next occurrence, bits 31..27 = variable reference type.

  gmref.py <data.win>        -> verification report (chain operands vs decoded instruction operands)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from gmcode import decode, PUSHES

VAR_TYPES = {0x00: "array", 0x80: "stacktop", 0xA0: "normal", 0xE0: "instance", 0x10: "arraypushaf", 0x90: "arraypopaf"}


def _walk_ok(buf, co, ce, first, occ, adj):
    a = first + adj
    for _ in range(occ):
        if not (co <= a < ce):
            return False
        a += struct.unpack_from("<I", buf, a)[0] & 0x07FFFFFF
    return True


def build_refs(dw):
    """Returns (var_refs, fn_refs, adj): dict operand_abs_addr -> (index, vartype_byte).
    The first-address adjustment (0 or +4) is detected per kind by validating whole chains."""
    buf = dw.buf
    co, cs = dw.chunks["CODE"]
    ce = co + cs
    out, adjs = {}, {}
    for kind, ents in (("v", dw.variables()), ("f", dw.functions())):
        items = [((e[3], e[4]) if kind == "v" else (e[1], e[2])) for e in ents]
        items = [(o, f) for o, f in items if o]
        adj = None
        for cand in (4, 0):
            if all(_walk_ok(buf, co, ce, f, o, cand) for o, f in items[:2000]):
                adj = cand
                break
        if adj is None:
            raise ValueError(f"no consistent first-address adjustment for {kind}")
        adjs[kind] = adj
        refs = {}
        for idx, e in enumerate(ents):
            occ, first = (e[3], e[4]) if kind == "v" else (e[1], e[2])
            if occ == 0:
                continue
            a = first + adj
            for _ in range(occ):
                w = struct.unpack_from("<I", buf, a)[0]
                refs[a] = (idx, w >> 24)
                a += w & 0x07FFFFFF
        out[kind] = refs
    return out["v"], out["f"], adjs


def operand_addrs(dw):
    """Set of operand-word absolute addresses of ref-carrying instructions (decoded), plus per-kind sets."""
    var_ops, fn_ops = set(), set()
    for name, ln, loc, args, bc in dw.code_entries():
        for i in decode(dw.buf, bc, ln):
            a = bc + i.addr + 4
            if i.op == 0x45 and i.t1 != 15:
                var_ops.add(a)
            elif i.op in PUSHES and i.op != 0x84 and i.t1 == 5:
                var_ops.add(a)
            elif i.op == 0xD9:
                fn_ops.add(a)
    return var_ops, fn_ops


def main():
    dw = DataWin(sys.argv[1])
    var_ops, fn_ops = operand_addrs(dw)
    print(f"decoded: var operands {len(var_ops)}  call operands {len(fn_ops)}")
    vr, fr, adjs = build_refs(dw)
    vs, fs = set(vr), set(fr)
    print(f"first-address adj: {adjs}")
    print(f"chain var {len(vs)} (match {len(vs & var_ops)}, extra {len(vs - var_ops)}, missing {len(var_ops - vs)}), "
          f"chain fn {len(fs)} (match {len(fs & fn_ops)}, extra {len(fs - fn_ops)}, missing {len(fn_ops - fs)})")


if __name__ == "__main__":
    main()
