#!/bin/sh
# Synthetic MPEG-2 test streams (ffmpeg testsrc2, 30 fps, 2 s = 60 frames, I/P only like the engine's videos).
# Also writes build/ref_<name>.txt: per-frame average RGB of a reference decode, for comparison with the console log.
set -e
cd "$(dirname "$0")"; mkdir -p build
for s in "352x288 s352" "640x480 s640"; do
  set -- $s
  ffmpeg -v error -y -f lavfi -i "testsrc2=size=$1:rate=30:duration=2" -c:v mpeg2video -pix_fmt yuv420p -bf 0 -g 12 \
         -b:v 2M -maxrate 2M -bufsize 1M -f mpeg2video build/$2.m2v
done
