"""audiomus.py <game> <data.win> <musdir>: music files in musdir whose name/stem appears as a string in data.win."""
import os, sys, csv
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from audiosurvey import probe_big, probe
game, win, mus = sys.argv[1:4]
S = {s.lower() for s in DataWin(win).strings()}
rows = []; tot_all = 0
for f in sorted(os.listdir(mus)):
    if not f.lower().endswith(".ogg"): continue
    p = os.path.join(mus, f); full = os.path.getsize(p); tot_all += full
    stem = f[:-4].lower()
    if stem in S or f.lower() in S or ("mus/" + f.lower()) in S:
        with open(p, "rb") as fh:
            head = fh.read(4096); fh.seek(max(0, full - 70000)); tail = fh.read(70000)
        i = probe_big(head, tail, full); rows.append((f, full, i["ch"], i["rate"], round(i["secs"], 1)))
with open(f"out/{game}_music.csv", "w", newline="") as fh:
    w = csv.writer(fh); w.writerow(["file", "bytes", "ch", "rate", "secs"]); w.writerows(rows)
mb = lambda v: v / 1048576
print(f"{game}: music files referenced {len(rows)} of {len(os.listdir(mus))} (dir {mb(tot_all):.0f} MB): {mb(sum(r[1] for r in rows)):.1f} MB, "
      f"{sum(r[4] for r in rows)/60:.0f} min, stereo {sum(1 for r in rows if r[2]==2)}, rates {sorted(set(r[3] for r in rows))}, "
      f"max file {max((r[1] for r in rows), default=0)/1048576:.1f} MB")
