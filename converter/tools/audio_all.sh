#!/bin/sh
cd "$(dirname "$0")/.."
G=../games
python3 tools/audiosurvey.py undertale "$G/UnderTale v1.0u3 Rus/data.win" "$G/UnderTale v1.0u3 Rus" > logs/audio_undertale.log 2>&1
python3 tools/audiosurvey.py yellow "$G/Undertale Yellow Rus/data.win" "$G/Undertale Yellow Rus" > logs/audio_yellow.log 2>&1
for c in 1 2 3 4 5; do
python3 tools/audiosurvey.py deltarune_ch$c "$G/DELTARUNE/chapter${c}_windows/data.win" "$G/DELTARUNE/chapter${c}_windows" "$G/DELTARUNE/mus" "$G/DELTARUNE" > logs/audio_deltarune_ch$c.log 2>&1
done
echo done > logs/audio_all.done
