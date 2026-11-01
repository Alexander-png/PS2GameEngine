#!/usr/bin/python3
"""gen_sounds.py <game> <data.win> <gen_dir> <deploy_dir>
Writes <gen_dir>/gm_sounds.c (table indexed by sound asset id) and <deploy_dir>/snd.bnk (SFX bodies, 16-byte aligned,
48-byte VAGp headers stripped); copies music .vas files to <deploy_dir> (only when missing / different size)."""
import csv, os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
game, win, gen, dep = sys.argv[1:5]
base = f"out/vag/{game}"
man = {r["name"]: r for r in csv.DictReader(open(f"{base}/manifest.csv"))}
snds = DataWin(win).sounds()
bank = bytearray(); rows = []
for s in snds:
    m = man.get(s["name"])
    if not m:
        rows.append((s["name"], -1, 0, 0, 0, 0, s["volume"])); continue
    if m["kind"] == "sfx":
        d = open(m["file"], "rb").read()[48:]
        off = len(bank); bank += d; bank += b"\0" * (-len(bank) % 16)
        rows.append((s["name"], 0, off, len(d), int(m["rate"]), len(d) // 16 * 28 * 1000 // int(m["rate"]), s["volume"]))
    else:
        rows.append((s["name"], 1, 0, 0, int(m["rate"]), int(m["blocks"]) * 28 * 1000 // int(m["rate"]), s["volume"]))
os.makedirs(dep, exist_ok=True)
open(f"{dep}/snd.bnk", "wb").write(bank)
n = 0
for s in snds:
    m = man.get(s["name"])
    if m and m["kind"] == "music":
        dst = f"{dep}/{s['name']}.vas"
        if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(m["file"]):
            shutil.copyfile(m["file"], dst); n += 1
with open(f"{gen}/gm_sounds.c", "w") as f:
    f.write('#include "gml/gm_sounds.h"\nconst gm_sounddef gm_sounds[] = {\n')
    for r in rows:
        f.write('    {"%s", %d, %d, %d, %d, %d, %g},\n' % r)
    f.write('};\nconst int gm_nsounds = %d;\n' % len(rows))
print(f"{len(rows)} sounds, bank {len(bank)} bytes, {n} music files copied")
