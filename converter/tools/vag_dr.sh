#!/bin/sh
cd "$(dirname "$0")/.."
G=../games
for c in 1 2 3 4 5; do
python3 tools/vag_convert.py deltarune_ch$c "$G/DELTARUNE/chapter${c}_windows/data.win" "$G/DELTARUNE/chapter${c}_windows" "$G/DELTARUNE/mus" "$G/DELTARUNE" --shared-music out/vag/deltarune_mus > logs/vag_deltarune_ch$c.log 2>&1
done
echo done > logs/vag_dr.done
