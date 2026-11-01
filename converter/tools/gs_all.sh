#!/bin/sh
cd "$(dirname "$0")/.."
G=../games
r() { n="$1"; w="$2"; t="$3"; /usr/bin/python3 -W ignore tools/gs_convert.py "$w" "$t" "$n" > "logs/gs_$n.log" 2>&1; echo "exit $?" >> "logs/gs_$n.log"; }
r undertale "$G/UnderTale v1.0u3 Rus/data.win" out/undertale
r yellow "$G/Undertale Yellow Rus/data.win" out/yellow_tex
for c in 1 2 3 4 5; do r deltarune_ch$c "$G/DELTARUNE/chapter${c}_windows/data.win" out/deltarune_ch$c; done
echo ALLDONE > logs/gs_all.done
