#!/usr/bin/python3
"""audiosurvey.py <game> <data.win> <dir> [<dir> ...]

Per-game audio inventory. Sources:
  embedded : data.win AUDO (group 0) and audiogroupN.dat next to data.win (SOND flags bit0, extra=(group, audioId))
  external : SOND `file` (or name+.ogg) searched in the given dirs
Parses OGG Vorbis id header + last granule, and RIFF WAV fmt/data, to get channels / rate / duration / bytes.
Writes out/<game>_audio.csv and prints a summary (SFX-like <= SFX_MAX_S seconds vs long = stream) with
PS2 ADPCM (VAG: 16 B per 28 samples per channel) size estimates.
"""
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin

SFX_MAX_S = 8.0


class AG(DataWin):
    def _parse_gen8(self):
        pass


def probe(b):
    """b: bytes-like. Returns dict(fmt, ch, rate, secs, bytes) or None."""
    n = len(b)
    if n >= 4 and bytes(b[:4]) == b"OggS":
        i = bytes(b[:4096]).find(b"\x01vorbis")
        if i < 0:
            return dict(fmt="ogg?", ch=0, rate=0, secs=0.0, bytes=n)
        ch = b[i + 11]
        rate = struct.unpack_from("<I", b, i + 12)[0]
        tail = bytes(b[max(0, n - 70000):n])
        j = tail.rfind(b"OggS")
        gran = struct.unpack_from("<q", tail, j + 6)[0] if j >= 0 and j + 14 <= len(tail) else 0
        return dict(fmt="ogg", ch=ch, rate=rate, secs=(gran / rate if rate and gran > 0 else 0.0), bytes=n)
    if n >= 12 and bytes(b[:4]) == b"RIFF":
        p = 12
        tag = ch = rate = bits = 0
        blk = 1
        data = 0
        while p + 8 <= n:
            cid = bytes(b[p:p + 4])
            sz = struct.unpack_from("<I", b, p + 4)[0]
            if cid == b"fmt ":
                tag, ch, rate, _, blk, bits = struct.unpack_from("<HHIIHH", b, p + 8)
            elif cid == b"data":
                data = min(sz, n - p - 8)
                break
            p += 8 + sz + (sz & 1)
        secs = data / (rate * blk) if rate and blk else 0.0
        return dict(fmt=f"wav{tag}/{bits}", ch=ch, rate=rate, secs=secs, bytes=n)
    return dict(fmt="?", ch=0, rate=0, secs=0.0, bytes=n)


def vag_bytes(secs, rate, ch, cap=None):
    r = min(rate, cap) if cap else rate
    samples = secs * r
    return ((int(samples) + 27) // 28) * 16 * ch


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return
    game, win = sys.argv[1], sys.argv[2]
    dirs = sys.argv[3:]
    dw = DataWin(win)
    snds = dw.sounds()
    base = os.path.dirname(win)
    groups = {0: dw}
    for fn in os.listdir(base):
        if fn.lower().startswith("audiogroup") and fn.lower().endswith(".dat"):
            gid = int("".join(c for c in fn if c.isdigit()))
            groups[gid] = AG(os.path.join(base, fn))
    gaudio = {g: d.audio() for g, d in groups.items()}
    idx = {}
    for d in dirs:
        for root, _, files in os.walk(d):
            if root != d and os.path.basename(root) in ("lang", "chapter1_windows", "chapter2_windows", "chapter3_windows",
                                                       "chapter4_windows", "chapter5_windows"):
                continue
            for f in files:
                if f.lower().endswith((".ogg", ".wav", ".mp3")):
                    idx.setdefault(f.lower(), os.path.join(root, f))
    rows, missing, seen_blob = [], 0, set()
    for s in snds:
        ex = s["extra"]
        gid, aid = (ex[0], ex[1]) if len(ex) >= 2 else (0, 0xFFFFFFFF)
        emb = bool(s["flags"] & 1)
        info = None
        src = "none"
        if emb and gid in gaudio and aid < len(gaudio[gid]):
            off, ln, _ = gaudio[gid][aid]
            if (gid, aid) in seen_blob:
                src = "emb-dup"
            else:
                seen_blob.add((gid, aid))
                src = f"emb{gid}"
            info = probe(groups[gid].buf[off:off + ln])
        else:
            f = (s["file"] or s["name"]).lower()
            if not f.endswith((".ogg", ".wav", ".mp3")):
                f += ".ogg"
            stem = os.path.splitext(f)[0]
            cands = [f, stem + ".ogg", s["name"].lower() + ".ogg"]
            for pre in ("mus_", "snd_"):
                if stem.startswith(pre):
                    cands.append(stem[len(pre):] + ".ogg")
                if s["name"].lower().startswith(pre):
                    cands.append(s["name"].lower()[len(pre):] + ".ogg")
            path = next((idx[c] for c in cands if c in idx), None)
            if path:
                with open(path, "rb") as fh:
                    b = fh.read(70000) if os.path.getsize(path) > 140000 else fh.read()
                    full = os.path.getsize(path)
                    if full > 140000:
                        fh.seek(full - 70000)
                        b2 = fh.read(70000)
                        # probe needs head+tail: stitch (head 4096 used for id header, tail for granule)
                        info = probe_big(b, b2, full)
                    else:
                        info = probe(b)
                src = "ext"
            else:
                missing += 1
        if info is None:
            rows.append((s["name"], src, gid, aid, 0, "", 0, 0, 0.0, 0, 0))
            continue
        rows.append((s["name"], src, gid, aid, info["bytes"], info["fmt"], info["ch"], info["rate"],
                     round(info["secs"], 2), vag_bytes(info["secs"], info["rate"], info["ch"]),
                     vag_bytes(info["secs"], info["rate"], 1, 22050)))
    os.makedirs("out", exist_ok=True)
    with open(f"out/{game}_audio.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["name", "src", "group", "audio_id", "bytes", "fmt", "ch", "rate", "secs", "vag_full_bytes", "vag_22k_mono_bytes"])
        w.writerows(rows)
    have = [r for r in rows if r[4] and r[1] != "emb-dup"]
    sfx = [r for r in have if r[8] <= SFX_MAX_S]
    lng = [r for r in have if r[8] > SFX_MAX_S]
    mb = lambda v: v / 1048576
    print(f"{game}: sounds {len(snds)}, unique blobs {len(have)}, missing {missing}, total {mb(sum(r[4] for r in have)):.1f} MB on disk")
    print(f"  SFX-like (<= {SFX_MAX_S:.0f}s): {len(sfx)}  disk {mb(sum(r[4] for r in sfx)):.1f} MB  "
          f"VAG full-rate {mb(sum(r[9] for r in sfx)):.1f} MB  VAG 22.05k mono {mb(sum(r[10] for r in sfx)):.1f} MB")
    print(f"  long (> {SFX_MAX_S:.0f}s): {len(lng)}  disk {mb(sum(r[4] for r in lng)):.1f} MB  total {sum(r[8] for r in lng)/60:.0f} min  "
          f"stereo {sum(1 for r in lng if r[6] == 2)}  rates {sorted(set(r[7] for r in lng))}")
    big = sorted(lng, key=lambda r: -r[4])[:3]
    print("  biggest long:", [(r[0], round(mb(r[4]), 1), r[8]) for r in big])
    fm = {}
    for r in have:
        fm[r[5]] = fm.get(r[5], 0) + 1
    print("  formats:", fm)


def probe_big(head, tail, full):
    """probe an OGG when only the head and the last 70000 bytes were read."""
    i = head[:4096].find(b"\x01vorbis")
    if i < 0 or head[:4] != b"OggS":
        return dict(fmt="?", ch=0, rate=0, secs=0.0, bytes=full)
    ch = head[i + 11]
    rate = struct.unpack_from("<I", head, i + 12)[0]
    j = tail.rfind(b"OggS")
    gran = struct.unpack_from("<q", tail, j + 6)[0] if j >= 0 else 0
    return dict(fmt="ogg", ch=ch, rate=rate, secs=(gran / rate if rate and gran > 0 else 0.0), bytes=full)


if __name__ == "__main__":
    main()
