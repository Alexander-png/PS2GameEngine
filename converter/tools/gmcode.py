#!/usr/bin/python3
"""gmcode.py - GML bytecode (version >= 15) decoder. Format reference: UndertaleModTool UndertaleCode.cs (GPL, format only).

Instruction = 32-bit word: opcode = bits 31..24; Single/Double/Cmp: type1 = bits 19..16, type2 = bits 23..20,
extra = bits 7..0 (dup count / cmp kind in bits 15..8); Goto: signed 24-bit word offset (x4 bytes);
Pop: type1 + instance type (bits 15..0) + 1 operand word (variable ref) unless type1 == Int16;
Push: data type in bits 23..16; payload by type; Call: 1 operand word (function ref); Break: sub-op in bits 15..0.

  gmcode.py <data.win> [--entry NAME] [--stats]
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

OPS = {
    0x07: "conv", 0x08: "mul", 0x09: "div", 0x0A: "rem", 0x0B: "mod", 0x0C: "add", 0x0D: "sub", 0x0E: "and",
    0x0F: "or", 0x10: "xor", 0x11: "neg", 0x12: "not", 0x13: "shl", 0x14: "shr", 0x15: "cmp",
    0x45: "pop", 0x84: "pushi", 0x86: "dup", 0x99: "callv", 0x9C: "ret", 0x9D: "exit", 0x9E: "popz",
    0xB6: "b", 0xB7: "bt", 0xB8: "bf", 0xBA: "pushenv", 0xBB: "popenv",
    0xC0: "push", 0xC1: "pushloc", 0xC2: "pushglb", 0xC3: "pushbltn", 0xD9: "call", 0xFF: "break",
}
DT = {0: "d", 1: "f", 2: "i", 3: "l", 4: "b", 5: "v", 6: "s", 7: "a", 8: "del", 9: "undef", 10: "u", 15: "e"}
CMP = {1: "<", 2: "<=", 3: "==", 4: "!=", 5: ">=", 6: ">"}
PUSH_EXTRA = {0: 8, 2: 4, 3: 8, 5: 4, 6: 4, 15: 0}
ARITH = set(range(0x07, 0x16))
GOTO = {0xB6, 0xB7, 0xB8, 0xBA, 0xBB}
PUSHES = {0xC0, 0xC1, 0xC2, 0xC3, 0x84}
SINGLE = {0x11, 0x12, 0x86, 0x9C, 0x9D, 0x9E, 0x99}


class Insn:
    __slots__ = ("addr", "op", "t1", "t2", "w", "extra", "arg", "size")

    def __repr__(self):
        return f"{self.addr:5d}: {OPS.get(self.op, hex(self.op))}.{DT.get(self.t1, self.t1)}.{DT.get(self.t2, self.t2)} {self.arg}"


def decode(buf, start, length):
    """Decode instructions in buf[start:start+length]. Returns list[Insn]. Raises ValueError on a bad stream."""
    out = []
    pos, end = start, start + length
    while pos < end:
        w = struct.unpack_from("<I", buf, pos)[0]
        op = w >> 24
        if op not in OPS:
            raise ValueError(f"unknown opcode {op:#x} at +{pos - start}")
        i = Insn()
        i.addr, i.op, i.w, i.size = pos - start, op, w, 4
        i.t1 = (w >> 16) & 0xF
        i.t2 = (w >> 20) & 0xF
        i.extra = w & 0xFF
        i.arg = None
        if op in GOTO:
            v = w & 0x7FFFFF  # 23-bit signed word offset (bit 22 = sign); 0xF00000 pattern = popenv exit marker
            if v & 0x400000:
                v -= 1 << 23
            i.arg = v  # offset in words (x4)
        elif op == 0x45:  # pop
            if i.t1 != 15:
                i.arg = struct.unpack_from("<i", buf, pos + 4)[0]
                i.size = 8
            else:
                i.arg = None
            i.t1, i.t2 = i.t1, i.t2
        elif op in PUSHES:
            t = (w >> 16) & 0xFF
            i.t1, i.t2 = t, 0
            if op == 0x84 and t == 15:
                i.arg = struct.unpack("<h", struct.pack("<H", w & 0xFFFF))[0]
            else:
                n = PUSH_EXTRA.get(t)
                if n is None:
                    raise ValueError(f"push data type {t} at +{pos - start}")
                i.size = 4 + n
                if t == 0:
                    i.arg = struct.unpack_from("<d", buf, pos + 4)[0]
                elif t == 3:
                    i.arg = struct.unpack_from("<q", buf, pos + 4)[0]
                elif n == 4:
                    i.arg = struct.unpack_from("<i", buf, pos + 4)[0]
        elif op == 0xD9:  # call
            i.arg = struct.unpack_from("<i", buf, pos + 4)[0]
            i.size = 8
        elif op == 0xFF:  # break
            t = (w >> 16) & 0xFF
            i.arg = struct.unpack("<h", struct.pack("<H", w & 0xFFFF))[0]
            i.t1 = t
            if t == 2:
                i.size = 8
        elif op == 0x15:
            i.arg = CMP.get((w >> 8) & 0xFF, (w >> 8) & 0xFF)
        pos += i.size
        out.append(i)
    if pos != end:
        raise ValueError(f"overran entry end by {pos - end} bytes")
    return out


def main():
    from gmwin import DataWin
    if len(sys.argv) < 2:
        print(__doc__)
        return
    dw = DataWin(sys.argv[1])
    ents = dw.code_entries()
    if "--entry" in sys.argv:
        name = sys.argv[sys.argv.index("--entry") + 1]
        for n, ln, loc, args, bc in ents:
            if n == name:
                for i in decode(dw.buf, bc, ln):
                    print(i)
        return
    ok = bad = total = 0
    ops = {}
    errs = []
    for n, ln, loc, args, bc in ents:
        try:
            ins = decode(dw.buf, bc, ln)
            ok += 1
            total += len(ins)
            for i in ins:
                ops[OPS[i.op]] = ops.get(OPS[i.op], 0) + 1
        except Exception as e:
            bad += 1
            if len(errs) < 8:
                errs.append((n, ln, str(e)))
    print(f"entries {len(ents)} ok {ok} bad {bad} instructions {total}")
    for e in errs:
        print("  bad:", e)
    print("opcode histogram:", dict(sorted(ops.items(), key=lambda kv: -kv[1])))


if __name__ == "__main__":
    main()
