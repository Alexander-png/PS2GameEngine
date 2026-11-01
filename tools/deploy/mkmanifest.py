#!/usr/bin/python3
"""mkmanifest.py <dir>   writes <dir>/manifest.txt for PS2Deployer: one line per file, "path<TAB>size<TAB>CRC32" (IEEE, upper-case hex).
Skips *.vas (unless --all), manifest.txt itself, the logs/ folder and hidden files."""
import os, sys, zlib

root = sys.argv[1]
SKIP = () if "--all" in sys.argv else (".vas",)   # music streams are large and static: left out of the manifest (already on the console); --all includes them again
lines = []
for base, dirs, files in os.walk(root):
    dirs[:] = sorted(d for d in dirs if d != "logs" and not d.startswith("."))
    for f in sorted(files):
        if f == "manifest.txt" or f.startswith(".") or f.endswith(SKIP):
            continue
        p = os.path.join(base, f)
        crc = 0
        with open(p, "rb") as fh:
            while True:
                b = fh.read(1 << 20)
                if not b:
                    break
                crc = zlib.crc32(b, crc)
        lines.append(f"{os.path.relpath(p, root)}\t{os.path.getsize(p)}\t{crc & 0xFFFFFFFF:08X}")
with open(os.path.join(root, "manifest.txt"), "w", newline="\n") as fh:
    fh.write("\n".join(lines) + "\n")
print(f"manifest.txt: {len(lines)} file(s)")
