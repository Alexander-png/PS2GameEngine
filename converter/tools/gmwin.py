#!/usr/bin/python3
"""gmwin.py - small read-only parser for GameMaker data.win (own code, format from public docs).

Covers the chunks needed first: GEN8, STRG, SOND, AUDO, FUNC, VARI, CODE.
All string references in data.win are absolute file offsets that point at the
character data (the u32 length sits 4 bytes before it).
"""
import struct


class DataWin:
    def __init__(self, path):
        import mmap
        self._f = open(path, "rb")
        self.buf = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)  # no full-file copy in RAM
        b = self.buf
        if b[:4] != b"FORM":
            raise ValueError("not a FORM file")
        end = 8 + struct.unpack_from("<I", b, 4)[0]
        self.chunks = {}
        self.chunk_order = []
        pos = 8
        while pos + 8 <= end:
            name = b[pos:pos + 4].decode("ascii")
            size = struct.unpack_from("<I", b, pos + 4)[0]
            self.chunks[name] = (pos + 8, size)
            self.chunk_order.append(name)
            pos += 8 + size
        self.tiles_ok = pos == end
        self._parse_gen8()

    # ---- helpers -------------------------------------------------------
    def u32(self, off):
        return struct.unpack_from("<I", self.buf, off)[0]

    def i32(self, off):
        return struct.unpack_from("<i", self.buf, off)[0]

    def f32(self, off):
        return struct.unpack_from("<f", self.buf, off)[0]

    def cstr(self, ptr):
        if ptr == 0:
            return None
        e = self.buf.find(b"\0", ptr)
        return self.buf[ptr:e].decode("utf-8", "replace")

    def ptr_list(self, chunk):
        """Chunks that start with u32 count + count absolute offsets."""
        off, _ = self.chunks[chunk]
        n = self.u32(off)
        return [self.u32(off + 4 + 4 * i) for i in range(n)]

    # ---- GEN8 ----------------------------------------------------------
    def _parse_gen8(self):
        off, _ = self.chunks["GEN8"]
        self.bytecode_version = self.buf[off + 1]
        self.game_file = self.cstr(self.u32(off + 4))
        self.config = self.cstr(self.u32(off + 8))
        self.game_id = self.u32(off + 20)
        self.name = self.cstr(self.u32(off + 0x28))
        self.gm_version = tuple(self.u32(off + 0x2C + 4 * i) for i in range(4))

    # ---- STRG ----------------------------------------------------------
    def strings(self):
        out = []
        for p in self.ptr_list("STRG"):
            n = self.u32(p)
            if self.buf[p + 4 + n] != 0:
                n = self.buf.find(b"\0", p + 4) - (p + 4)
            out.append(self.buf[p + 4:p + 4 + n].decode("utf-8", "replace"))
        return out

    def string_at(self, ptr):
        if ptr == 0:
            return None
        n = self.u32(ptr - 4)
        if self.buf[ptr + n] != 0:
            n = self.buf.find(b"\0", ptr) - ptr
        return self.buf[ptr:ptr + n].decode("utf-8", "replace")

    # ---- FUNC / VARI ---------------------------------------------------
    def functions(self):
        off, size = self.chunks["FUNC"]
        end = off + size
        res = []
        if self.bytecode_version >= 15:
            n = self.u32(off)
            p = off + 4
            for _ in range(n):
                name = self.string_at(self.u32(p))
                res.append((name, self.u32(p + 4), self.u32(p + 8)))
                p += 12
            # code-locals table follows: u32 count, then per code: u32 localsCount, u32 name, locals...
            self.code_locals = []
            # GM2.3+ builds omit this table (FUNC ends right after the function list)
            cn = self.u32(p) if p + 4 <= end else 0
            p += 4
            for _ in range(cn):
                lc = self.u32(p)
                cname = self.string_at(self.u32(p + 4))
                p += 8
                locs = []
                for _ in range(lc):
                    idx = self.u32(p)
                    locs.append((idx, self.string_at(self.u32(p + 4))))
                    p += 8
                self.code_locals.append((cname, locs))
        else:
            p = off
            while p + 12 <= end:
                res.append((self.string_at(self.u32(p)), self.u32(p + 4), self.u32(p + 8)))
                p += 12
        return res

    def variables(self):
        off, size = self.chunks["VARI"]
        end = off + size
        p = off
        if self.bytecode_version >= 15:
            self.vari_header = tuple(self.u32(off + 4 * i) for i in range(3))
            p += 12
        res = []
        if self.bytecode_version >= 15:
            # name, instanceType, varID, occurrences, firstAddress
            while p + 20 <= end:
                res.append((self.string_at(self.u32(p)), self.i32(p + 4), self.i32(p + 8),
                            self.u32(p + 12), self.u32(p + 16)))
                p += 20
        else:
            # old format: name, occurrences, firstAddress
            while p + 12 <= end:
                res.append((self.string_at(self.u32(p)), None, None,
                            self.u32(p + 4), self.u32(p + 8)))
                p += 12
        return res

    # ---- CODE ----------------------------------------------------------
    def code_entries(self):
        """Returns list of (name, length, locals, args, bytecode_offset)."""
        off, size = self.chunks["CODE"]
        res = []
        if size == 0:
            return res
        n = self.u32(off)
        ptrs = [self.u32(off + 4 + 4 * i) for i in range(n)]
        for p in ptrs:
            name = self.string_at(self.u32(p))
            length = self.u32(p + 4)
            if self.bytecode_version >= 15:
                locals_ = struct.unpack_from("<H", self.buf, p + 8)[0]
                args = struct.unpack_from("<H", self.buf, p + 10)[0] & 0x7FFF
                rel = self.i32(p + 12)
                bc = p + 12 + rel
            else:
                locals_, args = None, None
                bc = p + 8
            res.append((name, length, locals_, args, bc))
        return res

    # ---- SOND / AUDO ---------------------------------------------------
    def sounds(self):
        ptrs = self.ptr_list("SOND")
        off, size = self.chunks["SOND"]
        bounds = ptrs + [off + size]
        res = []
        for i, p in enumerate(ptrs):
            words = (bounds[i + 1] - p) // 4
            raw = struct.unpack_from("<%dI" % words, self.buf, p)
            res.append({
                "name": self.string_at(raw[0]),
                "flags": raw[1],
                "type": self.string_at(raw[2]),
                "file": self.string_at(raw[3]),
                "effects": raw[4],
                "volume": struct.unpack("<f", struct.pack("<I", raw[5]))[0],
                "pitch": struct.unpack("<f", struct.pack("<I", raw[6]))[0],
                "extra": raw[7:],
            })
        return res

    def audio(self):
        """Embedded audio blobs: list of (offset, length, magic)."""
        if "AUDO" not in self.chunks:
            return []
        res = []
        for p in self.ptr_list("AUDO"):
            n = self.u32(p)
            res.append((p + 4, n, self.buf[p + 4:p + 8]))
        return res


# ---- textures, texture-page entries, sprites ---------------------------------
def _png_end(buf, start):
    """Return end offset of a PNG that starts at `start` (walks the chunks)."""
    p = start + 8
    while p + 12 <= len(buf):
        n = struct.unpack_from(">I", buf, p)[0]
        typ = buf[p + 4:p + 8]
        p += 12 + n
        if typ == b"IEND":
            return p
    return len(buf)


def _textures(self):
    """List of (offset, length, kind) for each texture page blob; kind = 'png', 'qoi', 'bz2qoi' or '?'."""
    ptrs = self.ptr_list("TXTR")
    toff, tsize = self.chunks["TXTR"]
    res = []
    for p in ptrs:
        # entry: scaled u32, [more u32 fields in newer versions], blob pointer; find the word that points at a known magic
        blob = None
        for i in range(0, 10):
            if p + 4 * i + 4 > len(self.buf):
                break
            w = self.u32(p + 4 * i)
            if 0 < w < len(self.buf) - 8 and self.buf[w:w + 4] in (b"\x89PNG", b"fioq", b"2zoq", b"2ziq"):
                blob = w
                break
        if blob is None:
            res.append((0, 0, "?"))
            continue
        magic = self.buf[blob:blob + 4]
        if magic == b"\x89PNG":
            res.append((blob, _png_end(self.buf, blob) - blob, "png"))
        else:
            res.append((blob, 0, "qoi" if magic == b"fioq" else "bz2qoi"))
    return res


def _tpag(self):
    """Texture-page entries: dict offset -> (srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH, bndW, bndH, page)."""
    off, size = self.chunks["TPAG"]
    ptrs = self.ptr_list("TPAG")
    res = {}
    for p in ptrs:
        v = struct.unpack_from("<10Hh", self.buf, p)
        res[p] = v
    return res


def _sprites(self):
    """List of dict(name, w, h, originX, originY, frames=[tpag offsets], special) for SPRT."""
    res = []
    for p in self.ptr_list("SPRT"):
        name = self.string_at(self.u32(p))
        w, h = self.u32(p + 4), self.u32(p + 8)
        # margins 4x u32, transparent, smooth, preload, bboxMode, sepMasks, originX, originY
        ox, oy = self.i32(p + 0x30), self.i32(p + 0x34)
        q = p + 0x38
        special = False
        speed, stype_ = 1.0, 1      # GMS1: one frame per step at image_speed 1
        if self.i32(q) == -1:  # GMS2 special sprite header
            special = True
            q += 4
            sver = self.u32(q)
            stype = self.u32(q + 4)
            q += 8
            if self.gm_version[0] >= 2:
                speed, stype_ = self.f32(q), self.u32(q + 4)   # playback speed + type (0 = frames per second, 1 = per game frame)
                q += 8
            if sver >= 2:
                q += 4  # sequence offset
            if sver >= 3:
                q += 4  # nine-slice offset
            if stype != 0:
                res.append(dict(name=name, w=w, h=h, ox=ox, oy=oy, frames=[], special=True, stype=stype, speed=speed, speed_type=stype_))
                continue
        n = self.u32(q)
        if n > 100000:
            raise ValueError(f'bad sprite frame count {n} for {name} at {p:#x}')
        frames = [self.u32(q + 4 + 4 * i) for i in range(n)]
        res.append(dict(name=name, w=w, h=h, ox=ox, oy=oy, frames=frames, special=special, speed=speed, speed_type=stype_))
    return res


DataWin.textures = _textures
DataWin.tpag_entries = _tpag
DataWin.sprites = _sprites


# ---------------------------------------------------------------- OBJT / ROOM / BGND (usage analysis level)
def _plist(self, ptr):
    n = self.u32(ptr)
    if n > 1000000:
        raise ValueError(f"bad list count {n} at {ptr:#x}")
    return [self.u32(ptr + 4 + 4 * i) for i in range(n)]


def _backgrounds(self):
    """BGND: list of dict(name, tpag). Tileset/background texture entry (word 4)."""
    if "BGND" not in self.chunks:
        return []
    return [dict(name=self.string_at(self.u32(p)), tpag=self.u32(p + 16)) for p in self.ptr_list("BGND")]


def _objects(self):
    """OBJT: dict(name, sprite, mask, parent, depth, solid, persistent). Handles the 2022.5 'managed' flag by
    voting over all objects (hypothesis that keeps every field in range wins)."""
    ptrs = self.ptr_list("OBJT")
    nspr = len(self.ptr_list("SPRT")) if "SPRT" in self.chunks else 0
    nobj = len(ptrs)

    def ok(p, sh):
        pers = self.u32(p + 0x14 + sh)
        par = self.i32(p + 0x18 + sh)
        msk = self.i32(p + 0x1C + sh)
        spr = self.i32(p + 4)
        return pers in (0, 1) and (par == -100 or par == -1 or 0 <= par < nobj) and -1 <= msk < nspr and -1 <= spr < nspr
    sh = 0 if all(ok(p, 0) for p in ptrs) else (4 if all(ok(p, 4) for p in ptrs) else None)
    if sh is None:
        sh = 0 if sum(ok(p, 0) for p in ptrs) >= sum(ok(p, 4) for p in ptrs) else 4
    self._obj_sh = sh
    res = []
    for p in ptrs:
        res.append(dict(name=self.string_at(self.u32(p)), sprite=self.i32(p + 4), solid=self.u32(p + 0xC + sh),
                        depth=self.i32(p + 0x10 + sh), persistent=self.u32(p + 0x14 + sh),
                        parent=self.i32(p + 0x18 + sh), mask=self.i32(p + 0x1C + sh)))
    return res


def _rooms(self):
    """ROOM: dict(name, w, h, speed, objects=[object ids of instances], tiles_sprites/bgs, layer_sprites, tilesets)."""
    gms2 = self.gm_version[0] >= 2
    effects = gms2 and any(c in self.chunks for c in ("TGIN", "PSEM", "FEAT", "PSYS"))
    particles = gms2 and any(c in self.chunks for c in ("PSEM", "PSYS"))
    res = []
    for p in self.ptr_list("ROOM"):
        r = dict(name=self.string_at(self.u32(p)), w=self.u32(p + 8), h=self.u32(p + 12), speed=self.u32(p + 16),
                 persistent=self.u32(p + 20), creation_code=self.i32(p + 32), objects=[], sprites=set(),
                 tilesets=set(), bgs=set(), layer_types=[])
        bgs_p, views_p, objs_p, tiles_p = (self.u32(p + 4 * i) for i in (10, 11, 12, 13))
        wi = 15 if self.u32(p + 56) > 1 else 14
        for bp in self._plist(bgs_p):
            if self.u32(bp):  # enabled
                bg = self.i32(bp + 8)
                if bg >= 0:
                    r["bgs"].add(bg)
        for op in self._plist(objs_p):
            r["objects"].append(self.i32(op + 8))
        for tp in self._plist(tiles_p):
            r["sprites" if gms2 else "tilesets"].add(self.i32(tp + 8))
        if gms2:
            lay_p = self.u32(p + 4 * (wi + 8))
            for lp in self._plist(lay_p):
                lt = self.u32(lp + 8)
                r["layer_types"].append(lt)
                d = lp + 0x24
                if effects:
                    d = lp + 0x30 + 12 * self.u32(lp + 0x2C)
                if lt == 1:       # background layer: sprite id at +8
                    s = self.i32(d + 8)
                    if s >= 0:
                        r["sprites"].add(s)
                elif lt == 4:     # tile layer: tileset id
                    t = self.i32(d)
                    if t >= 0:
                        r["tilesets"].add(t)
                elif lt == 3:     # assets layer
                    first = self.u32(d)
                    n = 5 if (particles and first > d + 16) else (4 if particles else 3)
                    for tp in self._plist(self.u32(d)):
                        s = self.i32(tp + 8)
                        if s >= 0:
                            r["sprites"].add(s)
                    for sp_ in self._plist(self.u32(d + 4)):
                        s = self.i32(sp_ + 4)
                        if s >= 0:
                            r["sprites"].add(s)
        res.append(r)
    return res


DataWin._plist = _plist
DataWin.backgrounds = _backgrounds
DataWin.objects = _objects
DataWin.rooms = _rooms


EVENT_NAMES = ["Create", "Destroy", "Alarm", "Step", "Collision", "Keyboard", "Mouse", "Other", "Draw", "KeyPress",
               "KeyRelease", "Trigger", "CleanUp", "Gesture", "PreCreate"]


def _object_events(self):
    """Per object (OBJT order): list of (event_type, subtype, code_id, who). Event table follows the physics vertices
    inline: u32 type-count, type pointers; each type -> pointer list of events (subtype u32, action ptr list);
    action: libid,id,kind,relative,question,applyto,exetype,name,codeid(+0x20),argc,who,relative,not,0."""
    ptrs = self.ptr_list("OBJT")
    if not hasattr(self, "_obj_sh"):
        self.objects()
    sh = self._obj_sh
    res = []
    for p in ptrs:
        nvert = self.u32(p + 0x40 + sh)
        e0 = p + 0x50 + sh + 8 * nvert
        evs = []
        ntypes = self.u32(e0)
        if ntypes > 64:
            raise ValueError(f"bad event type count {ntypes} at {e0:#x}")
        for t in range(ntypes):
            lp = self.u32(e0 + 4 + 4 * t)
            for ep in self._plist(lp):
                sub = self.u32(ep)
                for ap in self._plist(ep + 4):
                    evs.append((t, sub, self.i32(ap + 0x20), self.i32(ap + 0x28)))
        res.append(evs)
    return res


DataWin.object_events = _object_events
