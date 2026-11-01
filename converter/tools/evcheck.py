#!/usr/bin/python3
"""evcheck.py <data.win> - parse object events and verify each maps to a code entry with the expected name."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin, EVENT_NAMES

d = DataWin(sys.argv[1])
ob, ev, ce = d.objects(), d.object_events(), d.code_entries()
tot = bad = mis = 0
used = set()
by_type = {}
for o, es in zip(ob, ev):
    for t, sub, cid, who in es:
        tot += 1
        by_type[EVENT_NAMES[t] if t < len(EVENT_NAMES) else t] = by_type.get(EVENT_NAMES[t] if t < len(EVENT_NAMES) else t, 0) + 1
        if not (0 <= cid < len(ce)):
            bad += 1
            continue
        used.add(cid)
        pre = f"gml_Object_{o['name']}_{EVENT_NAMES[t] if t < len(EVENT_NAMES) else t}_"
        if not ce[cid][0].startswith(pre):
            mis += 1
            if mis <= 4:
                print("mismatch", o["name"], t, sub, ce[cid][0])
print(f"objects {len(ob)} events {tot} bad_code_id {bad} name_mismatch {mis} distinct_code {len(used)} of {len(ce)}")
print("by type:", by_type)
