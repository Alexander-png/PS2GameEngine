#!/bin/sh
# vag_all.sh: convert all games' audio to SPU-ADPCM (SFX + music). DELTARUNE music tracks (mus/) are shared between chapters.
cd "$(dirname "$0")/.."
G=../games
[ -f out/vag/undertale/manifest.csv ] || python3 tools/vag_convert.py undertale "$G/UnderTale v1.0u3 Rus/data.win" "$G/UnderTale v1.0u3 Rus" > logs/vag_undertale.log 2>&1
python3 tools/vag_convert.py yellow "$G/Undertale Yellow Rus/data.win" "$G/Undertale Yellow Rus" > logs/vag_yellow.log 2>&1
for c in 1 2 3 4 5; do
python3 tools/vag_convert.py deltarune_ch$c "$G/DELTARUNE/chapter${c}_windows/data.win" "$G/DELTARUNE/chapter${c}_windows" "$G/DELTARUNE/mus" "$G/DELTARUNE" --shared-music out/vag/deltarune_mus > logs/vag_deltarune_ch$c.log 2>&1
done
echo done > logs/vag_all.done
