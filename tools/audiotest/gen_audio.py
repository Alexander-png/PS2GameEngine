#!/usr/bin/python3
"""gen_audio.py <vagdir> -> testaudio.bin : [u32 n][n x (u32 off,u32 len)] + blobs.
Entries: 0 snd_select, 1 snd_save, 2 snd_bell, 3 mus_st_him L, 4 mus_st_him R (de-interleaved from the 2048-byte chunk .vas)."""
import struct, sys, os
d = sys.argv[1] if len(sys.argv) > 1 else "../../converter/out/vag/undertale"
def vag(p): return open(p, "rb").read()[48:]
blobs = [vag(f"{d}/sfx/snd_select.vag"), vag(f"{d}/sfx/snd_save.vag"), vag(f"{d}/sfx/snd_bell.vag")]
m = vag(f"{d}/music/mus_st_him.vas")
L = b"".join(m[o:o + 2048] for o in range(0, len(m), 4096))
R = b"".join(m[o:o + 2048] for o in range(2048, len(m), 4096))
hdr = struct.pack("<I", len(blobs)); off = 4 + 8 * len(blobs); tab = b""
for b in blobs:
    tab += struct.pack("<II", off, len(b)); off += (len(b) + 63) // 64 * 64
out = hdr + tab
for b in blobs: out += b + b"\0" * (-len(b) % 64)
open("testaudio.bin", "wb").write(out)
print([len(b) for b in blobs], len(out))
