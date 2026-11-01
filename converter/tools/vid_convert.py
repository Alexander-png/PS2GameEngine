#!/usr/bin/python3
"""vid_convert.py: DELTARUNE videos -> PS2 formats.
 video : MPEG-2 elementary stream (IPU), 4:2:0, I/P only (no B), closed GOP 12, 640 wide, 16-aligned height
         (16:9 sources are scaled to 640x360 and padded with black to 640x368), CBR-ish, source frame rate kept
 audio : SPU-ADPCM stereo 44.1 kHz .vas (same streaming format as music, via engsnd), absent for silent sources
 sidecar .vinfo (little endian u32): magic 'GVID', width, height, fps_num, fps_den, frames, audio_blocks(0=none), audio_rate
Output: out/video/<name>.m2v [+ .vas] + .vinfo ; manifest out/video/manifest.csv"""
import csv, os, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vag_convert import vag_header, decode_pcm, encode, interleave
G = os.path.join(HERE, "../../games/DELTARUNE")
OUT = os.path.join(HERE, "../out/video")
JOBS = [("ch3_tenna_intro_en", "chapter3_windows/vid/tennaIntroF1_compressed_28.mp4"),
        ("ch3_tenna_intro_jp", "chapter3_windows/vid/tennaIntroJPf1_compressed_28.mp4"),
        ("ch3_tenna_intro_ru", "lang/chapter3/vid/tennaintrof1_compressed_28.mp4"),
        ("ch5_intro_en", "chapter5_windows/vid/ch5_intro_en.mp4"),
        ("ch5_intro_jp", "chapter5_windows/vid/ch5_intro_jp.mp4"),
        ("ch5_intro_ru", "lang/chapter5/vid/ch5_intro_en.mp4")]

def probe(src, sel, keys):
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", sel, "-show_entries", "stream=" + keys, "-of", "csv=p=0", src], capture_output=True, text=True)
    return r.stdout.strip()

def main():
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for name, rel in JOBS:
        src = os.path.join(G, rel)
        w, h, rate = probe(src, "v:0", "width,height,r_frame_rate").split(",")
        w, h = int(w), int(h)
        num, den = map(int, rate.split("/"))
        if w * 9 == h * 16:   # 16:9 -> 640x360 padded to 640x368
            vf = "scale=640:360:flags=lanczos,pad=640:368:0:4:black"; ow, oh = 640, 368
        else:
            vf = "scale=640:480:flags=lanczos"; ow, oh = 640, 480
        m2v = f"{OUT}/{name}.m2v"
        # CBR-like: min=max rate, VBV 224 KB (PS2 IPU/libmpeg friendly), no B frames, closed GOP
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", src, "-an", "-vf", vf + ",format=yuv420p", "-c:v", "mpeg2video",
                        "-profile:v", "main", "-level:v", "high" if False else "main", "-b:v", "3500k", "-minrate", "3500k", "-maxrate", "3500k",
                        "-bufsize", "1835008", "-bf", "0", "-g", "12", "-flags", "+cgop", "-sc_threshold", "1000000000",
                        "-qmin", "2", "-mbd", "rd", "-threads", "2", "-f", "mpeg2video", m2v], check=True)
        frames = int(subprocess.run(["ffprobe", "-v", "error", "-count_packets", "-select_streams", "v:0", "-show_entries", "stream=nb_read_packets", "-of", "csv=p=0", m2v],
                                    capture_output=True, text=True).stdout.strip().strip(","))
        ab, ar = 0, 44100
        if probe(src, "a:0", "codec_name"):
            chs = [encode(decode_pcm(src, ar, c), True) for c in range(2)]
            data = interleave(chs); ab = len(chs[0]) // 16
            open(f"{OUT}/{name}.vas", "wb").write(vag_header(len(data), ar, name, 2, ab) + data)
        open(f"{OUT}/{name}.vinfo", "wb").write(b"GVID" + struct.pack("<IIIIIII", ow, oh, num, den, frames, ab, ar))
        rows.append((name, ow, oh, f"{num}/{den}", frames, os.path.getsize(m2v), ab, ab and os.path.getsize(f"{OUT}/{name}.vas")))
        print(rows[-1], flush=True)
    with open(f"{OUT}/manifest.csv", "w", newline="") as f:
        wr = csv.writer(f); wr.writerow(["name", "w", "h", "fps", "frames", "m2v_bytes", "audio_blocks", "vas_bytes"]); wr.writerows(rows)

main()
