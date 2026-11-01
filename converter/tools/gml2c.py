#!/usr/bin/python3
"""gml2c.py <data.win> <out_dir> [--only SUBSTR] [--max N]   GML bytecode (>= 15) -> C.

Every code entry becomes one C function
    gm_val gml_<name>(gm_inst *self, gm_inst *other, int argc, const gm_val *argv)
The operand stack is mapped to C locals s0..sN (depth is static per instruction: verified for whole games by gmcfg.py),
basic-block labels are kept, GML variables go through the runtime (gm_get/gm_set by name id), `with` uses gm_with_*.
Besides the code it writes gm_tables.c (names, strings, function table, objects/events, scripts, rooms' code).

The runtime API used by the output is declared in engine/include/gml/gml.h.
References (format only): UndertaleModTool UndertaleCode.cs (GPL), Butterscotch (AGPL) - no code is copied.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gmcfg
from gmwin import DataWin
from gmcode import OPS, DT
from gmref import build_refs

CMP_C = {"<": "gm_lt", "<=": "gm_le", "==": "gm_eq", "!=": "gm_ne", ">=": "gm_ge", ">": "gm_gt"}
ARITH_C = {0x08: "gm_mul", 0x09: "gm_div", 0x0A: "gm_idiv", 0x0B: "gm_mod", 0x0C: "gm_add", 0x0D: "gm_sub", 0x0E: "gm_and",
           0x0F: "gm_or", 0x10: "gm_xor", 0x13: "gm_shl", 0x14: "gm_shr"}
ARG_RE = re.compile(r"^argument(\d+)$")


def sgn16(w):
    w &= 0xFFFF
    return w - 0x10000 if w & 0x8000 else w


def cstr(b):
    """C string literal for raw bytes."""
    out = []
    for c in b:
        if c == 0x22: out.append('\\"')
        elif c == 0x5C: out.append("\\\\")
        elif c == 0x0A: out.append("\\n")
        elif c == 0x0D: out.append("\\r")
        elif c == 0x09: out.append("\\t")
        elif 0x20 <= c < 0x7F and c != 0x3F: out.append(chr(c))   # '?' is escaped to avoid trigraphs
        else: out.append("\\%03o" % c)
    return '"' + "".join(out) + '"'


def mangle(name):
    return re.sub(r"[^0-9A-Za-z_]", "_", name)


class Ctx:
    """Game-wide tables shared by all functions."""

    def __init__(self, path):
        self.dw = DataWin(path)
        self.buf = self.dw.buf
        gmcfg.V2 = self.dw.bytecode_version >= 16
        self.v2 = gmcfg.V2
        self.vars_ = self.dw.variables()
        self.funcs = self.dw.functions()
        self.vr, self.fr, _ = build_refs(self.dw)
        self.entries = self.dw.code_entries()
        self.entry_by_name = {e[0]: n for n, e in enumerate(self.entries)}
        self.vid = {}          # variable name -> dense id
        self.vnames = []
        for v in self.vars_:
            self.var_id(v[0])
        self.strs = {}         # STRG index -> constant-string table index
        self.strg = None
        self.str_list = []
        self.used_funcs = set()
        self.diag = {}

    def var_id(self, name):
        if name not in self.vid:
            self.vid[name] = len(self.vnames)
            self.vnames.append(name)
        return self.vid[name]

    def string(self, idx):
        """push.s operand = index into the STRG table; returns the index in this game's constant-string table."""
        if idx not in self.strs:
            if self.strg is None:
                self.strg = self.dw.ptr_list("STRG")
            if not 0 <= idx < len(self.strg):
                raise ValueError(f"bad string index {idx}")
            p = self.strg[idx]
            n = struct.unpack_from("<I", self.buf, p)[0]
            if n > (1 << 20):
                raise ValueError(f"bad string length {n} at index {idx}")
            if self.buf[p + 4 + n] != 0:
                # localised builds patch strings in place and leave the old length prefix: the runner reads to the terminator
                n = self.buf.find(b"\0", p + 4) - (p + 4)
            self.strs[idx] = len(self.str_list)
            self.str_list.append(bytes(self.buf[p + 4:p + 4 + n]))
        return self.strs[idx]

    def bump(self, key):
        self.diag[key] = self.diag.get(key, 0) + 1


class Emit:
    def __init__(self, ctx, name, ln, nloc, nargs, bc):
        self.c, self.name, self.ln, self.nloc, self.nargs, self.bc = ctx, name, ln, nloc, nargs, bc
        self.locals = {}
        self.lines = []

    # ---- helpers ---------------------------------------------------------
    def emit(self, s):
        self.lines.append("    " + s)

    def local(self, name):
        if name not in self.locals:
            self.locals[name] = len(self.locals)
        return self.locals[name]

    def inst_expr(self, it, st_const, slot):
        """C expression of an instance operand. st_const: compile-time constant of the stack slot (or None)."""
        if st_const is not None:
            if st_const == -1: return "self"
            if st_const == -2: return "other"
            if st_const == -5: return "GM_GLOBAL"
            if st_const >= 0: return f"gm_obj1({st_const})"
        return f"gm_res(s{slot}, self, other)"

    def by_it(self, it):
        if it == -1: return "self"
        if it == -2: return "other"
        if it == -5: return "GM_GLOBAL"
        if it >= 0: return f"gm_obj1({it})"
        return None

    # ---- variable access ---------------------------------------------------
    def var_operands(self, i, st, d, is_pop):
        """Returns (kind, name, inst_expr, index_slot, value_slot, pops). kind: 'self'|'local'|'arg'|'argc'|'argv'|'arr'..."""
        c = self.c
        a = self.bc + i.addr + 4
        idx, vt = c.vr[a]
        name = c.vars_[idx][0]
        it = sgn16(i.w)
        top = st[-1][1] if st else None
        below = st[-2][1] if len(st) > 1 else None
        pops, _ = gmcfg.pp(c.buf, self.bc, i, top, below)
        ops = list(range(d - 1, d - 1 - pops, -1))       # slots popped, top first (for a pop the last one is the value)
        val = None
        if is_pop:
            if i.t1 == 2 and i.t2 == 5:
                # compound assignment (`a.b -= c`, `a[i] += c`): the compiler dups the target operands, loads the variable,
                # computes, so the NEW VALUE is on top and the instance / index operands lie below it (pop.i.v signature)
                val = ops[0]
                ops = ops[1:]
            else:
                val = ops[-1]
                ops = ops[:-1]
        consts = {k: st[k][1] for k in range(len(st))}
        arr = vt in (0x00, 0x10, 0x90)
        index = None
        inst = None
        if arr:
            index = ops[0]
            rest = ops[1:]
            if len(rest) == 2:                       # [instance, -9 marker, index]
                inst = self.inst_expr(it, consts.get(rest[1]), rest[1])
            else:
                inst = self.inst_expr(it, consts.get(rest[0]), rest[0]) if rest else (self.by_it(it) or "self")
        elif vt == 0x80 or it == -9:
            cand = [s for s in ops if consts.get(s) != -9]
            if cand:
                inst = self.inst_expr(it, consts.get(cand[0]), cand[0])
            else:
                inst = self.by_it(it) or "self"
        else:
            inst = self.by_it(it)
        if inst is None:
            inst = "self"
        return dict(name=name, vid=c.var_id(name), it=it, vt=vt, arr=arr, index=index, inst=inst, val=val, pops=pops, stack_inst=(vt == 0x80))

    def load_expr(self, o, i):
        name, it = o["name"], o["it"]
        if not o["arr"] and not o["stack_inst"]:
            if it == -7 or i.op == 0xC1:
                return f"L[{self.local(name)}]"
            m = ARG_RE.match(name)
            if m and it in (-15, -1, -7, -6):
                return f"GM_ARG({m.group(1)})"
            if name == "argument_count":
                return "gm_real(argc)"
        if o["arr"]:
            if name == "argument":
                return f"gm_arg_dyn(argc, argv, s{o['index']})"
            if it == -7 or i.op == 0xC1:
                return f"gm_geta_v(&L[{self.local(name)}], s{o['index']})"
            return f"gm_geta({o['inst']}, {o['vid']}, s{o['index']})"
        return f"gm_get({o['inst']}, {o['vid']})"

    def store_stmt(self, o, i, val):
        name, it = o["name"], o["it"]
        if not o["arr"] and not o["stack_inst"] and (it == -7 or i.op == 0xC1):
            return f"L[{self.local(name)}] = gm_keep({val});"
        if o["arr"]:
            if it == -7:
                return f"gm_seta_v(&L[{self.local(name)}], s{o['index']}, {val});"
            if o['inst'].startswith("gm_obj1("):
                return f"gm_seta_obj({o['inst'][8:-1]}, {o['vid']}, s{o['index']}, {val});"
            return f"gm_seta({o['inst']}, {o['vid']}, s{o['index']}, {val});"
        if o['inst'].startswith("gm_obj1("):
            return f"gm_set_obj({o['inst'][8:-1]}, {o['vid']}, {val});"
        return f"gm_set({o['inst']}, {o['vid']}, {val});"

    # ---- main --------------------------------------------------------------
    def build(self):
        c = self.c
        gmcfg.LAST = None
        ok, probs, nb, maxd = gmcfg.analyze(c.buf, self.bc, self.ln)
        if not ok:
            raise ValueError("stack analysis: " + "; ".join(probs))
        ins, state = gmcfg.LAST
        self.ins, self.state = ins, state
        targets = set()
        by_addr = {i.addr: n for n, i in enumerate(ins)}
        for i in ins:
            if i.op in (0xB6, 0xB7, 0xB8, 0xBA, 0xBB) and not (i.op == 0xBB and (i.w & 0xFFFFFF) == 0xF00000):
                targets.add(i.addr + i.arg * 4)
        # `with` nesting (structured): env depth before each instruction
        depth, envd = 0, {}
        for i in ins:
            envd[i.addr] = depth
            if i.op == 0xBA:
                depth += 1
            elif i.op == 0xBB and (i.w & 0xFFFFFF) != 0xF00000:
                depth -= 1
        self.maxenv = max(list(envd.values()) + [0]) + 1
        self.maxd = maxd + 2
        for n, i in enumerate(ins):
            if i.addr in targets:
                self.lines.append(f"L{i.addr}:;")
            if n not in state:
                continue          # unreachable
            self.insn(n, i, state[n], envd[i.addr], by_addr)
        self.lines.append(f"L{self.ln}:;")
        self.emit("return GM_UNDEF_V;")

    def end_envs(self, k):
        for e in range(k - 1, -1, -1):
            self.emit(f"gm_with_end(&W[{e}], &self, &other);")

    def insn(self, n, i, st, env, by_addr):
        c = self.c
        d = len(st)
        op = i.op
        emit = self.emit
        emit(f"/* {i.addr}: {OPS[op]}.{DT.get(i.t1, i.t1)}.{DT.get(i.t2, i.t2)} */") if os.environ.get("GM_TRACE") else None
        if op == 0x84:
            emit(f"s{d} = gm_real({i.arg});")
        elif op in (0xC0, 0xC1, 0xC2, 0xC3):
            t = i.t1
            if t == 15:
                emit(f"s{d} = gm_real({sgn16(i.w)});")
            elif t == 0:
                emit(f"s{d} = gm_real({i.arg!r});")
            elif t in (2, 3):
                emit(f"s{d} = gm_real({i.arg});")
            elif t == 6:
                emit(f"s{d} = gm_const_str({c.string(i.arg)});")
            elif t == 5:
                o = self.var_operands(i, st, d, False)
                base = d - o["pops"]
                emit(f"s{base} = {self.load_expr(o, i)};")
            elif t == 4:
                emit(f"s{d} = gm_real({1 if i.arg else 0});")
            else:
                emit(f"s{d} = GM_UNDEF_V; /* push type {t} */")
                c.bump(f"push type {t}")
        elif op == 0x45:
            if i.t1 == 15:
                return                                   # pop.e: discard (handled as popz)
            o = self.var_operands(i, st, d, True)
            emit(self.store_stmt(o, i, f"s{o['val']}"))
        elif op in ARITH_C:
            emit(f"s{d-2} = {ARITH_C[op]}(s{d-2}, s{d-1});")
        elif op == 0x15:
            emit(f"s{d-2} = {CMP_C[i.arg]}(s{d-2}, s{d-1});")
        elif op == 0x11:
            emit(f"s{d-1} = gm_neg(s{d-1});")
        elif op == 0x12:
            emit(f"s{d-1} = {'gm_lnot' if i.t1 == 4 else 'gm_bnot'}(s{d-1});")
        elif op == 0x07:  # conv.t1.t2
            t2 = i.t2
            if t2 == 2 or t2 == 3:
                emit(f"s{d-1} = gm_conv_i(s{d-1});")
            elif t2 == 4:
                emit(f"s{d-1} = gm_conv_b(s{d-1});")
            elif t2 == 6:
                emit(f"s{d-1} = gm_conv_s(s{d-1});")
            # to v/d/f/... : no representation change
        elif op == 0x86:  # dup
            k = self.dup_count(i, st)
            if (i.w >> 8) & 0x80:  # swap top k entries
                for j in range(k // 2):
                    a, b = d - 1 - j, d - k + j
                    emit(f"{{ gm_val t_ = s{a}; s{a} = s{b}; s{b} = t_; }}")
            else:
                for j in range(k):
                    emit(f"s{d + j} = s{d - k + j};")
        elif op == 0x9E:
            pass                                          # popz
        elif op == 0xB6:
            emit(f"goto L{i.addr + i.arg * 4};")
        elif op in (0xB7, 0xB8):
            cond = f"GM_T(s{d-1})"
            emit(f"if ({'' if op == 0xB7 else '!'}{cond}) goto L{i.addr + i.arg * 4};")
        elif op == 0xD9:
            fidx = c.fr[self.bc + i.addr + 4][0]
            argc = i.w & 0xFFFF
            c.used_funcs.add(fidx)
            if argc:
                args = ", ".join(f"s{d-1-j}" for j in range(argc))
                emit(f"{{ gm_val a_[{argc}] = {{{args}}}; s{d-argc} = gm_ftab[{fidx}](self, other, {argc}, a_); }}")
            else:
                emit(f"s{d} = gm_ftab[{fidx}](self, other, 0, 0);")
        elif op == 0x99:  # callv (method call): function value, instance, args
            argc = i.w & 0xFFFF
            args = ", ".join(f"s{d-1-j}" for j in range(argc)) if argc else "GM_UNDEF_V"
            emit(f"{{ gm_val a_[{max(argc,1)}] = {{{args}}}; s{d-argc-2} = gm_callv(s{d-argc-2}, s{d-argc-1}, self, other, {argc}, a_); }}")
        elif op == 0x9C:
            self.end_envs(env)
            emit(f"return s{d-1};")
        elif op == 0x9D:
            self.end_envs(env)
            emit("return GM_UNDEF_V;")
        elif op == 0xBA:  # pushenv
            top = st[-1][1]
            slot = d - 2 if (c.v2 and top == -9) else d - 1
            tgt = i.addr + i.arg * 4
            emit(f"if (!gm_with_begin(&W[{env}], s{slot}, &self, &other)) goto L{tgt};")
        elif op == 0xBB:  # popenv
            if (i.w & 0xFFFFFF) == 0xF00000:
                emit(f"gm_with_end(&W[{env-1}], &self, &other);")
            else:
                tgt = i.addr + i.arg * 4
                emit(f"if (gm_with_next(&W[{env-1}], &self, &other)) goto L{tgt};")
                emit(f"gm_with_end(&W[{env-1}], &self, &other);")
        elif op == 0xFF:  # break (GMS2.3+ sub-ops)
            self.brk(i, st, d)
        else:
            emit(f"/* unhandled {OPS[op]} */")
            c.bump(f"unhandled {OPS[op]}")

    def dup_count(self, i, st):
        n = (i.extra & 0x7F) + 1
        need = n * gmcfg.tsize(i.t1)
        got, k = 0, 0
        while got < need and k < len(st):
            k += 1
            got += st[-k][0]
        return k

    def brk(self, i, st, d):
        sub = i.arg
        pops, pushes = gmcfg.BREAK_PP.get(sub, (0, 0))
        e = self.emit
        names = {-1: "chkindex", -2: "pushaf", -3: "popaf", -4: "pushac", -5: "setowner", -6: "isstaticok", -7: "setstatic",
                 -8: "savearef", -9: "restorearef", -10: "chkindex2", -11: "pushref"}
        nm = names.get(sub, f"sub{sub}")
        self.c.bump(f"break {nm}")
        if sub == -5 or sub == -7 or sub == -1:
            return                                   # owner / static markers / index check: no effect here
        if sub == -6:
            e(f"s{d} = gm_real(1); /* isstaticok */")
            return
        args = ", ".join(f"s{d-1-j}" for j in range(pops)) or ""
        if pushes:
            e(f"s{d-pops} = gm_break_{nm.replace('-', 'm')}({args}{', ' if args else ''}self, other); ")
        else:
            e(f"gm_break_{nm}({args}{', ' if args else ''}self, other);")

    def text(self):
        slots = ", ".join(f"s{k}" for k in range(self.maxd))
        body = f"    gm_val {slots};\n"
        if self.locals:
            body += f"    gm_val L[{len(self.locals)}]; for (int k_ = 0; k_ < {len(self.locals)}; k_++) L[k_] = GM_UNDEF_V;\n"
        if self.maxenv > 1:
            body += f"    gm_with_t W[{self.maxenv}];\n"
        hdr = f"gm_val gml_{mangle(self.name)}(gm_inst *self, gm_inst *other, int argc, const gm_val *argv)\n{{\n"
        return hdr + body + "\n".join(self.lines) + "\n}\n"


def scripts(dw):
    """SCPT: list of (name, code entry index)."""
    out = []
    if "SCPT" not in dw.chunks:
        return out
    for p in dw.ptr_list("SCPT"):
        out.append((dw.string_at(dw.u32(p)), dw.i32(p + 4) & 0x7FFFFFFF))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return
    flags = sys.argv[1:]
    only = flags[flags.index("--only") + 1] if "--only" in flags else None
    maxn = int(flags[flags.index("--max") + 1]) if "--max" in flags else None
    ctx = Ctx(args[0])
    out = args[1]
    os.makedirs(out, exist_ok=True)
    chunk, nfile, nfn, nfail = [], 0, 0, 0
    fails = {}
    emitted = []

    def flush():
        nonlocal chunk, nfile
        if not chunk:
            return
        with open(os.path.join(out, f"gm_code_{nfile:03d}.c"), "w") as f:
            f.write('#include "gm_game.h"\n\n' + "\n".join(chunk))
        nfile += 1
        chunk = []

    for n, (name, ln, loc, nargs, bc) in enumerate(ctx.entries):
        if only and only not in name:
            continue
        if maxn is not None and nfn >= maxn:
            break
        if ln == 0:
            chunk.append(f"gm_val gml_{mangle(name)}(gm_inst *self, gm_inst *other, int argc, const gm_val *argv)\n{{ return GM_UNDEF_V; }}\n")
            emitted.append(name)
            nfn += 1
            continue
        e = Emit(ctx, name, ln, loc, nargs, bc)
        try:
            e.build()
            chunk.append(e.text())
            emitted.append(name)
            nfn += 1
        except Exception as ex:
            nfail += 1
            k = str(ex)[:50]
            fails[k] = fails.get(k, 0) + 1
            chunk.append(f"gm_val gml_{mangle(name)}(gm_inst *self, gm_inst *other, int argc, const gm_val *argv)\n{{ gm_unsupported(\"{name}\"); return GM_UNDEF_V; }}\n")
            emitted.append(name)
        if len(chunk) >= 300:
            flush()
    flush()
    write_tables(ctx, out, emitted)
    write_objects(ctx, out, emitted)
    write_rooms(ctx, out, emitted)
    write_sprites(ctx, out)
    print(f"entries {len(ctx.entries)} emitted {nfn} failed {nfail} files {nfile} vars {len(ctx.vnames)} strings {len(ctx.str_list)} funcs used {len(ctx.used_funcs)}")
    for k, v in sorted(fails.items(), key=lambda kv: -kv[1])[:8]:
        print("  fail", v, k)
    print("diag:", dict(sorted(ctx.diag.items(), key=lambda kv: -kv[1])))


def write_tables(ctx, out, emitted):
    dw = ctx.dw
    es = set(emitted)
    scr = scripts(dw)
    with open(os.path.join(out, "gm_game.h"), "w") as f:
        f.write('#ifndef GM_GAME_H\n#define GM_GAME_H\n#include "gml/gml.h"\n')
        for name in emitted:
            f.write(f"gm_val gml_{mangle(name)}(gm_inst *self, gm_inst *other, int argc, const gm_val *argv);\n")
        f.write("#endif\n")
    with open(os.path.join(out, "gm_tables.c"), "w") as f:
        f.write('#include "gm_game.h"\n\n')
        f.write(f"const int gm_nvars = {len(ctx.vnames)};\nconst char *const gm_var_names[] = {{\n")
        for v in ctx.vnames:
            f.write(f"    {cstr(v.encode())},\n")
        f.write("};\n\n")
        f.write(f"const int gm_nstrs = {len(ctx.str_list)};\ngm_str gm_strs[] = {{\n")
        for s in ctx.str_list:
            f.write(f"    {{-1, {len(s)}, {cstr(s)}}},\n")
        f.write("};\n\n")
        # function table: scripts resolve to their code, others to runtime builtins gmb_<name> (weak fallback in the runtime)
        script_code = {}
        for sname, cid in scr:
            if 0 <= cid < len(ctx.entries):
                script_code[sname] = ctx.entries[cid][0]
        nf = len(ctx.funcs)
        weak_done = set()
        f.write(f"const int gm_nfuncs = {nf};\n")
        for k, (fname, occ, first) in enumerate(ctx.funcs):
            tgt = None
            for cand in (f"gml_Script_{fname}", fname):
                if cand in es:
                    tgt = cand
            if tgt is None and fname in script_code and script_code[fname] in es:
                tgt = script_code[fname]
            if tgt:
                f.write(f"gm_val gml_{mangle(tgt)}(gm_inst *, gm_inst *, int, const gm_val *);\n")
            else:
                m = mangle(fname)
                if m not in weak_done:
                    weak_done.add(m)
                    f.write(f'GM_WEAK gm_val gmb_{m}(gm_inst *s, gm_inst *o, int argc, const gm_val *argv) {{ (void)s; (void)o; (void)argc; (void)argv; return gm_unimpl({cstr(fname.encode())}); }}\n')
        f.write(f"const gm_fn gm_ftab[{max(nf,1)}] = {{\n")
        names = []
        for k, (fname, occ, first) in enumerate(ctx.funcs):
            tgt = None
            for cand in (f"gml_Script_{fname}", fname):
                if cand in es:
                    tgt = cand
            if tgt is None and fname in script_code and script_code[fname] in es:
                tgt = script_code[fname]
            f.write(f"    {('gml_' + mangle(tgt)) if tgt else ('gmb_' + mangle(fname))},\n")
        f.write("};\n")
        f.write(f"const char *const gm_func_names[] = {{\n")
        for fname, occ, first in ctx.funcs:
            f.write(f"    {cstr(fname.encode())},\n")
        f.write("};\n\n")
        # scripts by asset index
        f.write(f"const int gm_nscripts = {len(scr)};\nconst gm_script_def gm_scripts[] = {{\n")
        for sname, cid in scr:
            code = ctx.entries[cid][0] if 0 <= cid < len(ctx.entries) else None
            fn = f"gml_{mangle(code)}" if code and code in es else "0"
            f.write(f"    {{{cstr(sname.encode())}, {fn}}},\n")
        f.write("};\n")


def write_objects(ctx, out, emitted):
    """gm_objs: object definitions with their event handlers (code entries of OBJT events)."""
    dw = ctx.dw
    es = set(emitted)
    objs, evs = dw.objects(), dw.object_events()
    ptrs = dw.ptr_list("OBJT")
    with open(os.path.join(out, "gm_objects.c"), "w") as f:
        f.write('#include "gm_game.h"\n#include "gml/gml_rt.h"\n\n')
        for k, ev in enumerate(evs):
            rows = []
            for (t, sub, cid, who) in ev:
                if 0 <= cid < len(ctx.entries) and ctx.entries[cid][0] in es:
                    rows.append((t, sub, "gml_" + mangle(ctx.entries[cid][0])))
            if rows:
                f.write(f"static const gm_objevent ev_{k}[] = {{\n")
                for t, sub, fn in rows:
                    f.write(f"    {{{t}, {sub}, {fn}}},\n")
                f.write("};\n")
            evs[k] = (ev, len(rows))
        f.write(f"const int gm_nobjs = {len(objs)};\nconst gm_objdef gm_objs[] = {{\n")
        for k, o in enumerate(objs):
            vis = dw.u32(ptrs[k] + 8)
            n = evs[k][1]
            par = o["parent"] if 0 <= o["parent"] < len(objs) else -1
            f.write(f"    {{{cstr(o['name'].encode())}, {o['sprite']}, {par}, {o['depth']}, {o['mask']}, {1 if vis else 0}, "
                    f"{1 if o['solid'] else 0}, {1 if o['persistent'] else 0}, {n}, {('ev_%d' % k) if n else 'NULL'}}},\n")
        f.write("};\n")


def write_rooms(ctx, out, emitted):
    """gm_rooms: size, speed, creation code, instances (x, y, object, id, creation code)."""
    dw = ctx.dw
    es = set(emitted)

    def code(cid):
        if 0 <= cid < len(ctx.entries) and ctx.entries[cid][0] in es:
            return "gml_" + mangle(ctx.entries[cid][0])
        return "NULL"
    ptrs = dw.ptr_list("ROOM")
    with open(os.path.join(out, "gm_rooms.c"), "w") as f:
        f.write('#include "gm_game.h"\n#include "gml/gml_rt.h"\n\n')
        for k, p in enumerate(ptrs):
            ins = dw._plist(dw.u32(p + 4 * 12))
            if ins:
                f.write(f"static const gm_roominst ri_{k}[] = {{\n")
                for ip in ins:
                    f.write(f"    {{{dw.i32(ip)}, {dw.i32(ip + 4)}, {dw.i32(ip + 8)}, {dw.i32(ip + 12)}, {code(dw.i32(ip + 16))}}},\n")
                f.write("};\n")
        f.write(f"const int gm_nrooms = {len(ptrs)};\nconst gm_roomdef gm_rooms[] = {{\n")
        for k, p in enumerate(ptrs):
            n = len(dw._plist(dw.u32(p + 4 * 12)))
            views = []
            for vp in dw._plist(dw.u32(p + 4 * 11))[:8]:
                views.append("{" + ", ".join(str(dw.i32(vp + 4 * j)) for j in range(14)) + "}")
            views += ["{0}"] * (8 - len(views))
            f.write(f"    {{{cstr(dw.string_at(dw.u32(p)).encode())}, {dw.u32(p + 8)}, {dw.u32(p + 12)}, {dw.u32(p + 16)}, "
                    f"{1 if dw.u32(p + 20) else 0}, {code(dw.i32(p + 32))}, {n}, {('ri_%d' % k) if n else 'NULL'}, {{{', '.join(views)}}}}},\n")
        f.write("};\n")


def _mask_is_rect(dw, off, w, h, l, r, b, t):
    """True when every pixel inside the sprite's bounding box is solid (so the box alone describes the mask)."""
    st = (w + 7) // 8
    for y in range(max(t, 0), min(b, h - 1) + 1):
        for x in range(max(l, 0), min(r, w - 1) + 1):
            if not (dw.buf[off + y * st + (x >> 3)] >> (7 - (x & 7))) & 1:
                return False
    return True


def write_sprites(ctx, out):
    """gm_sprites: size, origin, collision bbox margins, frame count."""
    dw = ctx.dw
    sp = dw.sprites()
    ptrs = dw.ptr_list("SPRT")
    with open(os.path.join(out, "gm_sprites.c"), "w") as f:
        f.write('#include "gml/gml_rt.h"\n\n')
        rows, masks = [], []
        for k, (p, s_) in enumerate(zip(ptrs, sp)):
            l, r, b, t = (dw.i32(p + 0x0C + 4 * i) for i in range(4))
            mref = "0"
            # precise collision masks (GM:S 1.x layout: one 1bpp mask after the frame list); only for big precise sprites, where a bounding box is too coarse
            if (not s_.get("special") and dw.gm_version[0] < 2 and dw.u32(p + 0x2C) == 1 and s_["frames"]):
                q = p + 0x38 + 4 + 4 * len(s_["frames"])
                mc = dw.u32(q)
                nb = ((s_["w"] + 7) // 8) * s_["h"]
                # skip masks that are just the full rectangle (bounding box already says it all)
                if mc >= 1 and not _mask_is_rect(dw, q + 4, s_["w"], s_["h"], l, r, b, t):
                    masks.append(f"static const unsigned char gmk_{k}[] = {{" + ",".join(str(dw.buf[q + 4 + j]) for j in range(nb)) + "};\n")
                    mref = f"gmk_{k}"
            rows.append(f"    {{{cstr(s_['name'].encode())}, {s_['w']}, {s_['h']}, {s_['ox']}, {s_['oy']}, {l}, {r}, {b}, {t}, {len(s_['frames'])}, {mref}}},\n")
        f.writelines(masks)
        f.write(f"const int gm_nsprites = {len(sp)};\nconst gm_spritedef gm_sprites[] = {{\n")
        f.writelines(rows)
        f.write("};\n\n")
        pp = dw.ptr_list("PATH") if "PATH" in dw.chunks else []
        for k, q in enumerate(pp):
            n = dw.u32(q + 16)
            f.write(f"static const float gp_{k}[] = {{" + ", ".join(f"{dw.f32(q + 20 + 4 * j):.3f}f" for j in range(3 * n)) + "};\n")
        f.write(f"const int gm_npaths = {len(pp)};\nconst gm_pathdef gm_paths[] = {{\n")
        for k, q in enumerate(pp):
            f.write(f"    {{{dw.u32(q + 4)}, {dw.u32(q + 8)}, {dw.u32(q + 12)}, {dw.u32(q + 16)}, gp_{k}}},\n")
        if not pp:
            f.write("    {0, 0, 0, 0, 0},\n")
        f.write("};\n")


if __name__ == "__main__":
    main()
