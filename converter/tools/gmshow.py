#!/usr/bin/python3
"""gmshow.py <gen_dir> <function name or substring> [...]

Prints generated C functions with variable ids and function-table slots replaced by their GameMaker names, which makes
the generated code readable enough to follow a cutscene:  gm_get(self, 273) -> gm_get(self, /*con*/ 273).
Substring match on the C function name (e.g. obj_undyneencounter2_Step_0). Run from anywhere."""
import glob, os, re, sys

gen = sys.argv[1]
want = sys.argv[2:]
tab = open(os.path.join(gen, "gm_tables.c")).read().split("\n")


def names(start_pat):
    out, on = [], False
    for l in tab:
        if l.startswith(start_pat):
            on = True
            continue
        if on:
            if l.startswith("};"):
                break
            out.append(l.strip().rstrip(",").strip('"'))
    return out


var = names("const char *const gm_var_names[]")
fn = names("const char *const gm_func_names[]")


def annotate(body):
    body = re.sub(r"gm_(get|set|geta|seta)\((self|other|GM_GLOBAL), (\d+)",
                  lambda m: "gm_%s(%s, /*%s*/ %s" % (m[1], m[2], var[int(m[3])] if int(m[3]) < len(var) else "?", m[3]), body)
    body = re.sub(r"gm_ftab\[(\d+)\]", lambda m: "gm_ftab[/*%s*/ %s]" % (fn[int(m[1])] if int(m[1]) < len(fn) else "?", m[1]), body)
    return body


for f in sorted(glob.glob(os.path.join(gen, "gm_code_*.c"))):
    s = open(f).read()
    for m in re.finditer(r"\ngm_val (gml_\w+)\(.*?\n\}\n", s, re.S):
        if any(w in m[1] for w in want):
            print("//", os.path.basename(f))
            print(annotate(m[0]))
