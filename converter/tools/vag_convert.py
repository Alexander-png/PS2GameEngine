#!/usr/bin/python3
"""vag_convert.py <game> <data.win> <dir> [<dir> ...] [--limit N] [--music-rate 44100] [--only sfx|music] [--verify N]

Converts a game's audio to PS2 SPU-ADPCM (own encoder tools/vagenc, standard 16-byte blocks).
 SFX   (secs <= 8)  : mono 22050 Hz, one-shot. block0 flag 0x04 (loop-start), last block 0x01 (end);
                      engine patches the last block to 0x03 for looping sounds.   -> out/vag/<game>/sfx/<name>.vag
 music (secs  > 8)  : stereo (mono stays mono) at --music-rate, each channel encoded as a looped stream
                      (0x06 ... 0x03), interleaved in 2048-byte chunks (L,R,L,R...) -> out/vag/<game>/music/<name>.vas
 Both carry a 48-byte VAGp header (big endian: 'VAGp', ver 0x20, size, rate, name) so stock players open them.
 Sources: same resolution as audiosurvey.py (embedded AUDO / audiogroupN.dat, external files in <dir>s).
 Also DELTARUNE's mus/ tracks from out/<game>_music.csv.
 Writes out/vag/<game>/manifest.csv: kind,name,file,ch,rate,blocks,bytes,snr_db(optional).
"""
import csv, os, struct, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gmwin import DataWin
from audiosurvey import AG

HERE = os.path.dirname(os.path.abspath(__file__))
VAGENC = os.path.join(HERE, "vagenc")
SFX_MAX_S = 8.0
CHUNK = 2048


def vag_header(size, rate, name, ch=1, loop_blocks=0):
    """standard VAGp layout: 0x00 'VAGp', 0x04 ver, 0x0C size, 0x10 rate, 0x1E channels, 0x20 name.
    Non-standard (in the reserved area): 0x14 = ADPCM blocks per channel before the zero padding (BE)."""
    h = b"VAGp" + struct.pack(">IIII", 0x20, 0, size, rate) + struct.pack(">I", loop_blocks) + b"\0" * 6
    h += bytes([ch, 0]) + name.encode("ascii", "replace")[:16].ljust(16, b"\0")
    return h.ljust(48, b"\0")


def decode_pcm(src_bytes_or_path, rate, ch_pick):
    """ffmpeg -> raw s16le mono (ch_pick 0/1 channel, or -1 = downmix) at `rate`."""
    cmd = ["ffmpeg", "-v", "error", "-i", "pipe:0" if isinstance(src_bytes_or_path, bytes) else src_bytes_or_path]
    if ch_pick >= 0:
        cmd += ["-af", f"pan=mono|c0=c{ch_pick}"]
    else:
        cmd += ["-ac", "1"]
    cmd += ["-ar", str(rate), "-f", "s16le", "pipe:1"]
    r = subprocess.run(cmd, input=src_bytes_or_path if isinstance(src_bytes_or_path, bytes) else None,
                       capture_output=True)
    return r.stdout


def encode(pcm, loop):
    return subprocess.run([VAGENC, "1" if loop else "0"], input=pcm, capture_output=True).stdout


def dec_adpcm(b):
    F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]
    s1 = s2 = 0
    out = []
    for o in range(0, len(b) - 15, 16):
        p, sh = b[o] >> 4, b[o] & 15
        f0, f1 = F[min(p, 4)]
        for i in range(28):
            n = (b[o + 2 + i // 2] >> (4 * (i & 1))) & 15
            if n > 7:
                n -= 16
            s = ((n << 12) >> sh) + ((s1 * f0 + s2 * f1) >> 6)
            s = max(-32768, min(32767, s))
            out.append(s)
            s2, s1 = s1, s
    return out


def snr(pcm, adpcm):
    import math
    x = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
    y = dec_adpcm(adpcm)[:len(x)]
    num = sum(v * v for v in x)
    den = sum((a - b) ** 2 for a, b in zip(x, y)) or 1
    return 10 * math.log10(num / den) if num else 99.0


def interleave(chs):
    if len(chs) == 1:
        return chs[0]
    n = max(len(c) for c in chs)
    n = (n + CHUNK - 1) // CHUNK * CHUNK
    out = bytearray()
    for o in range(0, n, CHUNK):
        for c in chs:
            blk = c[o:o + CHUNK]
            out += blk.ljust(CHUNK, b"\0")
    return bytes(out)


def main():
    a = sys.argv[1:]
    if len(a) < 3:
        print(__doc__)
        return
    opt = {"--limit": 0, "--music-rate": 44100, "--only": "", "--verify": 0, "--names": "", "--shared-music": ""}
    rest = []
    i = 0
    while i < len(a):
        if a[i] in opt:
            opt[a[i]] = type(opt[a[i]])(a[i + 1])
            i += 2
        else:
            rest.append(a[i])
            i += 1
    game, win, dirs = rest[0], rest[1], rest[2:]
    dw = DataWin(win)
    base = os.path.dirname(win)
    groups = {0: dw}
    for fn in os.listdir(base):
        if fn.lower().startswith("audiogroup") and fn.lower().endswith(".dat"):
            groups[int("".join(c for c in fn if c.isdigit()))] = AG(os.path.join(base, fn))
    gaudio = {g: d.audio() for g, d in groups.items()}
    idx = {}
    for d in dirs:
        for root, _, files in os.walk(d):
            if root != d and os.path.basename(root).startswith(("chapter", "lang")):
                continue
            for f in files:
                if f.lower().endswith((".ogg", ".wav", ".mp3")):
                    idx.setdefault(f.lower(), os.path.join(root, f))
    jobs, seen = [], set()  # (name, bytes|path)
    for s in dw.sounds():
        ex = s["extra"]
        gid, aid = (ex[0], ex[1]) if len(ex) >= 2 else (0, 0xFFFFFFFF)
        if s["flags"] & 1 and gid in gaudio and aid < len(gaudio[gid]):
            if (gid, aid) in seen:
                continue
            seen.add((gid, aid))
            off, ln, _ = gaudio[gid][aid]
            jobs.append((s["name"], bytes(groups[gid].buf[off:off + ln])))
        else:
            f = (s["file"] or s["name"]).lower()
            if not f.endswith((".ogg", ".wav", ".mp3")):
                f += ".ogg"
            stem = os.path.splitext(f)[0]
            c = [f, stem + ".ogg", s["name"].lower() + ".ogg"]
            for pre in ("mus_", "snd_"):
                if stem.startswith(pre):
                    c.append(stem[len(pre):] + ".ogg")
                if s["name"].lower().startswith(pre):
                    c.append(s["name"].lower()[len(pre):] + ".ogg")
            p = next((idx[x] for x in c if x in idx), None)
            if p:
                jobs.append((s["name"], p))
    mcsv = f"out/{game}_music.csv"
    if os.path.exists(mcsv):
        for r in csv.DictReader(open(mcsv)):
            p = idx.get(r["file"].lower())
            if p:
                jobs.append((os.path.splitext(r["file"])[0], p, True))
    if opt["--limit"]:
        jobs = jobs[:opt["--limit"]]
    root = f"out/vag/{game}"
    for k in ("sfx", "music"):
        os.makedirs(f"{root}/{k}", exist_ok=True)
    nver = [0]

    def probe(src):
        cmd = ["ffprobe", "-v", "error", "-show_entries", "stream=channels,sample_rate:format=duration",
               "-of", "csv=p=0", "pipe:0" if isinstance(src, bytes) else src]
        o = subprocess.run(cmd, input=src if isinstance(src, bytes) else None, capture_output=True).stdout.decode().split()
        try:
            rate, ch = map(int, o[0].split(","))
            return ch, rate, 0.0
        except Exception:
            return 0, 0, 0.0

    def work(j):
        name, src = j[0], j[1]
        shared = len(j) > 2
        ch, rate, secs = probe(src)
        if not ch:
            return None
        pcm = decode_pcm(src, 22050, -1)
        secs = len(pcm) / 44100.0
        if secs <= 0:
            return None
        if opt["--names"] and name not in opt["--names"].split(","):
            return None
        if os.environ.get("VAGDUMP") == name:
            open("/tmp/vagdump_%s.s16" % name, "wb").write(decode_pcm(src, 44100, 0))
        kind = "music" if (shared or secs > SFX_MAX_S) else "sfx"  # mus/ tracks (loaded by name as streams) are always music
        if opt["--only"] and opt["--only"] != kind:
            return None
        safe = "".join(c if c.isalnum() or c in "_-." else "_" for c in name)
        if kind == "sfx":
            r = 22050
            ad = bytearray(encode(pcm, False))
            if len(ad) >= 16:
                ad[1] = 0x04 if len(ad) > 16 else 0x05
                ad[-15] = 0x01
            data = bytes(ad)
            path = f"{root}/sfx/{safe}.vag"
            open(path, "wb").write(vag_header(len(data), r, safe) + data)
            sn = snr(pcm, data) if nver[0] < opt["--verify"] else ""
            if sn != "":
                nver[0] += 1
            return (kind, name, path, 1, r, len(data) // 16, len(data), f"{sn:.1f}" if sn != "" else "")
        r = opt["--music-rate"]
        nc = 2 if ch >= 2 else 1
        if shared and opt["--shared-music"]:
            os.makedirs(opt["--shared-music"], exist_ok=True)
            path = f'{opt["--shared-music"]}/{safe}.vas'
            if os.path.exists(path):  # already converted by another chapter: reuse, just read its header
                hd = open(path, "rb").read(48)
                return (kind, name, path, nc, r, struct.unpack(">I", hd[0x14:0x18])[0], os.path.getsize(path) - 48, "")
        chs = [encode(decode_pcm(src, r, c if nc == 2 else -1), True) for c in range(nc)]
        data = interleave(chs)
        if not (shared and opt["--shared-music"]):
            path = f"{root}/music/{safe}.vas"
        open(path, "wb").write(vag_header(len(data), r, safe, nc, len(chs[0]) // 16) + data)
        return (kind, name, path, nc, r, len(chs[0]) // 16, len(data), "")

    with ThreadPoolExecutor(8) as ex:
        res = [r for r in ex.map(work, jobs) if r]
    with open(f"{root}/manifest.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["kind", "name", "file", "ch", "rate", "blocks", "bytes", "snr_db"])
        w.writerows(res)
    for k in ("sfx", "music"):
        rr = [r for r in res if r[0] == k]
        print(f"{game} {k}: {len(rr)} files, {sum(r[6] for r in rr) / 1048576:.1f} MB")
    sn = [float(r[7]) for r in res if r[7]]
    if sn:
        print(f"SNR over {len(sn)} verified SFX: min {min(sn):.1f} avg {sum(sn)/len(sn):.1f} dB")


if __name__ == "__main__":
    main()
