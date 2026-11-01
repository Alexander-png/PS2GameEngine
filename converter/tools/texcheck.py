#!/usr/bin/python3
"""texcheck.py <data.win>: texture/tpag/sprite structural check (read-only)."""
import os, sys
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
d = DataWin(sys.argv[1])
t = d.textures(); tp = d.tpag_entries(); sp = d.sprites()
bad = sum(1 for s in sp for f in s["frames"] if f not in tp)
pages = max((v[10] for v in tp.values()), default=-1) + 1
print(sys.argv[1].split("games/")[-1], "| textures", len(t), dict(Counter(k for _, _, k in t)),
      "| tpag", len(tp), "pages used", pages, "| sprites", len(sp), "bad frame ptrs", bad,
      "| special", sum(1 for s in sp if s["special"]))
print("  sample:", [(s["name"], s["w"], s["h"], len(s["frames"])) for s in sp[:4]])
