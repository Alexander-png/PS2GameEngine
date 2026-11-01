#!/bin/sh
cd "$(dirname "$0")/.."
G=../games
r() { n="$1"; w="$2"; t="$3"; /usr/bin/python3 tools/spritereport.py "$w" "$t" "out/${n}_sprites.csv" > "logs/sprites_$n.log" 2>&1; echo "exit $?" >> "logs/sprites_$n.log"; }
r yellow "$G/Undertale Yellow Rus/data.win" out/yellow_tex
r deltarune_root "$G/DELTARUNE/data.win" out/deltarune_root
for c in 1 2 3 4 5; do r deltarune_ch$c "$G/DELTARUNE/chapter${c}_windows/data.win" out/deltarune_ch$c; done
echo ALLDONE > logs/report_all.done
