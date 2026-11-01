#!/usr/bin/python3
"""gmcfg.py - basic blocks + operand-stack analysis for GML bytecode (>= 15).

For every code entry: decode, split into basic blocks and run an abstract interpretation of the operand stack
(depth + known integer constants, needed to recognise the GMS2 instance marker -9). It verifies that every join sees
one depth, nothing underflows and every branch target is valid. Verifying the stack model over a whole game is what
makes the later C emitter (stack -> locals) trustworthy.

  gmcfg.py <data.win> [--entry NAME]   (without --entry: whole-game statistics)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmcode import decode, OPS, GOTO

LAST = None
V2 = False  # bytecode >= 16: stack-instance variable access carries an instance marker value (-9 = instance below)
# Break sub-ops (GMS2.3+): value -> (pops, pushes)
BREAK_PP = {-1: (0, 0), -2: (2, 1), -3: (3, 0), -4: (1, 1), -5: (1, 0), -6: (0, 1), -7: (0, 0), -8: (0, 0),
            -9: (0, 0), -10: (0, 0), -11: (0, 1)}
ARITH2 = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x13, 0x14, 0x15}  # pop 2 push 1


def vartype_of(buf, bc, i):
    return struct.unpack_from("<I", buf, bc + i.addr + 4)[0] >> 24


def itype_of(i):
    return struct.unpack("<h", struct.pack("<H", i.w & 0xFFFF))[0]


def pp(buf, bc, i, top, below=None):
    """(pops, pushes) of instruction i. `top` = known constant on top of the stack (or None)."""
    op = i.op
    if op in ARITH2:
        return 2, 1
    if op in (0x07, 0x11, 0x12):
        return 1, 1
    if op == 0x84:
        return 0, 1
    if op in (0xC0, 0xC1, 0xC2, 0xC3):
        if i.t1 == 5:  # variable
            vt = vartype_of(buf, bc, i)
            it = itype_of(i)
            if vt in (0x00, 0x10, 0x90):  # array element / array reference (GMS2.3 pushaf/popaf): [instance/marker, index]
                return (3 if (V2 and below == -9) else 2), 1  # -9 marker => instance value sits below it
            if it == -9 or vt == 0x80:
                if V2 and vt == 0x80 and it != -9:
                    return (2 if top == -9 else 1), 1
                return 1, 1  # instance taken from the stack
            return 0, 1
        return 0, 1
    if op == 0x45:  # pop
        if i.t1 == 15:
            return 1, 0
        vt = vartype_of(buf, bc, i)
        it = itype_of(i)
        if vt == 0x00:
            return (4 if (V2 and below == -9) else 3), 0
        if it == -9 or vt == 0x80:
            if V2 and vt == 0x80 and it != -9:
                return (3 if (top == -9 or below == -9) else 2), 0
            return 2, 0
        return 1, 0
    if op == 0x86:  # dup: copies (extra+1) values; flag 0x80 in bits 15..8 = swap
        n = (i.extra & 0x7F) + 1
        return (n, n) if (i.w >> 8) & 0x80 else (0, n)
    if op == 0xD9:
        return (i.w & 0xFFFF), 1
    if op in (0xB7, 0xB8, 0x9E, 0x9C):
        return 1, 0
    if op == 0xBA:  # pushenv: instance (+ -9 marker in V2 when the instance is a stack value)
        return (2 if (V2 and top == -9) else 1), 0
    if op == 0x99:  # callv: function + instance + argc args -> result
        return (i.w & 0xFFFF) + 2, 1
    if op == 0xFF:
        return BREAK_PP.get(i.arg, (0, 0))
    return 0, 0


def build(buf, bc, ln):
    ins = decode(buf, bc, ln)
    by_addr = {i.addr: n for n, i in enumerate(ins)}
    leaders = {0}
    for n, i in enumerate(ins):
        if i.op in GOTO:
            if not (i.op == 0xBB and (i.w & 0xFFFFFF) == 0xF00000):
                leaders.add(i.addr + i.arg * 4)
            if n + 1 < len(ins):
                leaders.add(ins[n + 1].addr)
        elif i.op in (0x9C, 0x9D) and n + 1 < len(ins):
            leaders.add(ins[n + 1].addr)
    return ins, by_addr, sorted(l for l in leaders if l in by_addr or l == ln)


TSIZE = {0: 8, 1: 4, 2: 4, 3: 8, 4: 4, 5: 16, 6: 16, 7: 4, 8: 4, 9: 16, 10: 4, 15: 4}


def tsize(t):
    return TSIZE.get(t, 16)


def push_size(i):
    """Size in bytes of the single value pushed by instruction i."""
    op = i.op
    if op in (0xC0, 0xC1, 0xC2, 0xC3):
        return tsize(i.t1)
    if op == 0x84:
        return 4
    if op == 0x07:  # conv.t1.t2
        return tsize(i.t2)
    if op in (0x11, 0x12):
        return tsize(i.t1)
    if op in ARITH2 and op != 0x15:
        return tsize(i.t2)
    if op == 0x15:
        return 4
    if op == 0xFF:
        return 4 if i.arg in (-6, -11) else 16
    return 16  # call, callv, variable reads


def step(buf, bc, i, st):
    """Apply instruction i to abstract stack tuple st of (size, const). Returns new tuple or None on underflow."""
    top = st[-1][1] if st else None
    below = st[-2][1] if len(st) > 1 else None
    pops, pushes = pp(buf, bc, i, top, below)
    if pops > len(st):
        return None
    op = i.op
    if op == 0x86 and (i.w >> 8) & 0x80:  # swap: reorders values, depth unchanged; contents/consts become unknown
        k = min(len(st), 4)
        return st[:len(st) - k] + tuple((sz, None) for sz, _ in st[len(st) - k:])
    if op == 0x86:
        n = (i.extra & 0x7F) + 1
        need = n * tsize(i.t1)
        got, k = 0, 0
        while got < need and k < len(st):
            k += 1
            got += st[-k][0]
        if got != need:
            return None
        if (i.w >> 8) & 0x80:  # swap: reverse the two top groups conservatively
            return st[:-k] + tuple((sz, None) for sz, _ in reversed(st[-k:]))
        return st + st[-k:]
    if op == 0x84:
        return st + ((4, i.arg if i.t1 == 15 else None),)
    if op == 0xC0 and i.t1 == 2:
        return st + ((4, i.arg),)
    if op == 0x07:  # conv: keeps a constant, changes the size
        return st[:-1] + ((tsize(i.t2), st[-1][1]),)
    base = st[:len(st) - pops] if pops else st
    if pushes:
        return base + ((push_size(i), None),) * pushes
    return base


def merge(a, b):
    return tuple((x[0], x[1] if x == y else None) if x[0] == y[0] else (x[0], None) for x, y in zip(a, b))


def analyze(buf, bc, ln):
    """Returns (ok, problems list, nblocks, max_depth)."""
    ins, by_addr, leaders = build(buf, bc, ln)
    if not ins:
        return True, [], 0, 0
    problems = []
    state = {}
    work = [(0, ())]
    maxd = 0
    visits = 0
    while work and not problems:
        n, st = work.pop()
        while n < len(ins):
            visits += 1
            if visits > 400000:
                problems.append("analysis did not converge")
                break
            old = state.get(n)
            if old is not None:
                if len(old) != len(st):
                    problems.append(f"join mismatch at +{ins[n].addr}: {len(old)} vs {len(st)}")
                    break
                m = merge(old, st)
                if m == old:
                    break
                st = m
            state[n] = st
            i = ins[n]
            nst = step(buf, bc, i, st)
            if nst is None:
                problems.append(f"underflow at +{i.addr} {OPS[i.op]} (depth {len(st)})")
                break
            maxd = max(maxd, len(nst), len(st))
            if i.op in (0x9C, 0x9D):
                break
            if i.op in GOTO:
                if i.op == 0xBB and (i.w & 0xFFFFFF) == 0xF00000:
                    st = nst
                    n += 1
                    continue
                tgt = i.addr + i.arg * 4
                tn = by_addr.get(tgt, len(ins) if tgt == ln else None)
                if tn is None:
                    problems.append(f"bad branch target {tgt} at +{i.addr}")
                    break
                if i.op == 0xB6:
                    n = tn
                    st = nst
                    continue
                work.append((tn, nst))
            st = nst
            n += 1
    global LAST
    LAST = (ins, state)
    return (not problems), problems[:3], len(leaders), maxd


def main():
    from gmwin import DataWin
    if len(sys.argv) < 2:
        print(__doc__)
        return
    dw = DataWin(sys.argv[1])
    global V2
    V2 = dw.bytecode_version >= 16
    print("bytecode", dw.bytecode_version, "V2", V2)
    ents = dw.code_entries()
    if "--entry" in sys.argv:
        name = sys.argv[sys.argv.index("--entry") + 1]
        for n, ln, loc, args, bc in ents:
            if n == name:
                ins, by_addr, leaders = build(dw.buf, bc, ln)
                print(f"{n}: {len(ins)} insns, {len(leaders)} blocks, leaders {leaders[:20]}")
                print(analyze(dw.buf, bc, ln))
        return
    ok = bad = blocks = 0
    maxd = 0
    errs = {}
    shown = []
    for n, ln, loc, args, bc in ents:
        if ln == 0:
            ok += 1
            continue
        try:
            good, probs, nb, md = analyze(dw.buf, bc, ln)
        except Exception as e:
            good, probs, nb, md = False, [f"exception {e}"], 0, 0
        blocks += nb
        maxd = max(maxd, md)
        if good:
            ok += 1
        else:
            bad += 1
            kind = probs[0].split(" at ")[0].split(" (")[0][:28]
            errs[kind] = errs.get(kind, 0) + 1
            if len(shown) < 6:
                shown.append((n, probs[0]))
    print(f"entries {len(ents)} ok {ok} bad {bad} blocks {blocks} max_depth {maxd}")
    print("problem kinds:", errs)
    for s in shown:
        print("  ", s)


if __name__ == "__main__":
    main()
