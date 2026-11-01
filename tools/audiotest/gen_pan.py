#!/usr/bin/python3
"""gen_pan.py -> panaudio.bin : cyber_battle SFX stems (arp, bongo, chord) as [u32 n][n x (off,len)] + blobs (VAG header stripped)."""
import struct
d = "../../converter/out/vag/deltarune_ch2/sfx/cyber_battle_backing_"
blobs = [open(d + n + ".vag", "rb").read()[48:] for n in ("arp", "bongo", "chord")]
off = 4 + 8 * len(blobs); tab = b""
for b in blobs:
    tab += struct.pack("<II", off, len(b)); off += (len(b) + 63) // 64 * 64
out = struct.pack("<I", len(blobs)) + tab
for b in blobs: out += b + b"\0" * (-len(b) % 64)
open("panaudio.bin", "wb").write(out); print([len(b) for b in blobs])
