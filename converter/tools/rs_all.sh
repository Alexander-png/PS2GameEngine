#!/bin/sh
cd "$(dirname "$0")/.."
G=../games
r() { n="$1"; w="$2"; /usr/bin/python3 tools/roomsets.py "$w" "out/${n}_sprites.csv" "$n" > "logs/rs_$n.log" 2>&1; echo "exit $?" >> "logs/rs_$n.log"; }
r yellow "$G/Undertale Yellow Rus/data.win"
r undertale "$G/UnderTale v1.0u3 Rus/data.win"
for c in 1 2 3 4 5; do r deltarune_ch$c "$G/DELTARUNE/chapter${c}_windows/data.win"; done
echo ALLDONE > logs/rs_all.done
