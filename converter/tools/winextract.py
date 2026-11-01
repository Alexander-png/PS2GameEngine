#!/usr/bin/python3
"""winextract.py textures <data.win> <outdir>  - write every texture page as PNG (read-only on the game).

PNG pages are copied as-is. GameMaker 2022.x pages are BZip2+QOI ('2zoq') or QOI ('fioq') and are
decoded through the small C helper ./gmqoi (built from gmqoi.c). One page at a time to keep memory low.
"""
import bz2
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gmwin import DataWin


def decode_qoi_body(body, w, h):
    p = subprocess.run([os.path.join(HERE, "gmqoi"), str(w), str(h)], input=body, capture_output=True, check=True)
    return p.stdout


def save_rgba(raw, w, h, path):
    from PIL import Image
    Image.frombytes("RGBA", (w, h), raw).save(path)


def extract(dw, idx, off, length, kind, outdir):
    path = os.path.join(outdir, f"texture_{idx:03d}.png")
    buf = dw.buf
    if kind == "png":
        with open(path, "wb") as f:
            f.write(buf[off:off + length])
        return path
    w, h = struct.unpack_from("<HH", buf, off + 4)
    if kind == "qoi":
        n = struct.unpack_from("<I", buf, off + 8)[0]
        body = bytes(buf[off + 12:off + 12 + n])
    else:  # bz2qoi: header is 8 bytes, +4 for the uncompressed size in 2022.5+
        body = None
        for hdr in (12, 8):
            try:
                d = bz2.BZ2Decompressor()
                data = d.decompress(bytes(buf[off + hdr:off + hdr + (w * h * 4) + 4096 * 64]))
                if data[:4] == b"fioq":
                    body = data[12:]
                elif hdr == 12 and len(data) == struct.unpack_from("<I", buf, off + 8)[0]:
                    body = data
                if body is not None:
                    break
            except Exception:
                continue
        if body is None:
            raise ValueError("could not decompress bz2qoi page")
    save_rgba(decode_qoi_body(body, w, h), w, h, path)
    return path


def main():
    if len(sys.argv) < 4 or sys.argv[1] != "textures":
        print(__doc__)
        return
    dw = DataWin(sys.argv[2])
    outdir = sys.argv[3]
    os.makedirs(outdir, exist_ok=True)
    pages = dw.textures()
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else len(pages)
    for i, (off, length, kind) in enumerate(pages[:limit]):
        if kind == "?":
            print(f"page {i}: unknown format")
            continue
        print(f"page {i}: {kind} -> {extract(dw, i, off, length, kind, outdir)}", flush=True)


if __name__ == "__main__":
    main()
