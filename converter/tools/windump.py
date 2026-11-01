#!/usr/bin/python3
"""windump.py - summary of a data.win using gmwin.py. Read-only.
Usage: windump.py <data.win> [--functions] [--strings N]
"""
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin


def main():
    path = sys.argv[1]
    dw = DataWin(path)
    print(f"== {path}")
    print(f"   {dw.name!r} bytecode {dw.bytecode_version} GM {dw.gm_version} tiles_ok={dw.tiles_ok}")
    strings = dw.strings()
    print(f"   strings: {len(strings)}")
    funcs = dw.functions()
    print(f"   functions referenced: {len(funcs)}")
    varis = dw.variables()
    print(f"   variables: {len(varis)}")
    codes = dw.code_entries()
    # GM2.3+ child entries share their parent's bytecode, so the sum of entry lengths overcounts;
    # the CODE chunk size is the real amount of bytecode.
    print(f"   code entries: {len(codes)}, CODE chunk bytes: {dw.chunks['CODE'][1]}")
    snd = dw.sounds()
    aud = dw.audio()
    print(f"   sounds: {len(snd)}, embedded audio: {len(aud)} "
          f"({Counter(a[2] for a in aud).most_common(3)})")
    if "--functions" in sys.argv:
        for name, occ, first in funcs:
            print(f"      {name}  x{occ}")
    if "--strings" in sys.argv:
        n = int(sys.argv[sys.argv.index("--strings") + 1])
        for s in strings[:n]:
            print("      ", repr(s[:80]))


if __name__ == "__main__":
    main()
