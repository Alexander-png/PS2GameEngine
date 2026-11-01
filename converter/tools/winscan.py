#!/usr/bin/python3
"""winscan.py - read-only scan of a GameMaker data.win (FORM/IFF container).

Prints GEN8 info (bytecode version, GM version, game name) and the chunk list.
Usage: winscan.py <path/to/data.win> [...]
"""
import struct
import sys


def read_cstr(buf, off):
    if off <= 0 or off >= len(buf):
        return None
    end = buf.find(b"\0", off)
    return buf[off:end].decode("utf-8", "replace")


def scan(path):
    with open(path, "rb") as f:
        head = f.read(8)
        if head[:4] != b"FORM":
            print(f"{path}: not a FORM file (magic {head[:4]!r})")
            return
        form_size = struct.unpack("<I", head[4:])[0]
        chunks = []
        pos = 8
        end = 8 + form_size
        while pos + 8 <= end:
            f.seek(pos)
            name, size = struct.unpack("<4sI", f.read(8))
            chunks.append((name.decode("ascii", "replace"), pos + 8, size))
            pos += 8 + size
        gen8 = next((c for c in chunks if c[0] == "GEN8"), None)
        print(f"== {path}")
        print(f"   FORM size {form_size}, {len(chunks)} chunks, file end ok: {pos == end}")
        if gen8:
            f.seek(gen8[1])
            g = f.read(gen8[2])
            dbg, bc = g[0], g[1]
            name_ptr, cfg_ptr = struct.unpack_from("<II", g, 4)
            f.seek(0)
            whole_needed = True
            major, minor, rel, build = struct.unpack_from("<4I", g, 0x2C)
            # name/config pointers are file offsets
            def s(ptr):
                f.seek(ptr)
                data = f.read(256)
                end_i = data.find(b"\0")
                return data[:end_i].decode("utf-8", "replace") if end_i >= 0 else None
            filename = s(name_ptr)
            config = s(cfg_ptr)
            print(f"   GEN8: bytecode version {bc}, GM {major}.{minor}.{rel}.{build}, "
                  f"debug-disabled={dbg}, file='{filename}', config='{config}'")
        for name, off, size in chunks:
            print(f"   {name}  offset {off:>10}  size {size:>10}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        scan(p)
