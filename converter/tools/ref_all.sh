#!/bin/sh
cd "$(dirname "$0")/.."
G=../games
r() { n="$1"; w="$2"; /usr/bin/python3 tools/gmref.py "$w" > "logs/ref_$n.log" 2>&1; echo "exit $?" >> "logs/ref_$n.log"; }
r yellow "$G/Undertale Yellow Rus/data.win"
r deltarune_root "$G/DELTARUNE/data.win"
for c in 1 2 3 4 5; do r deltarune_ch$c "$G/DELTARUNE/chapter${c}_windows/data.win"; done
echo ALLDONE > logs/ref_all.done
