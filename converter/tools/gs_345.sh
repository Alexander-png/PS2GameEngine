#!/bin/sh
cd "$(dirname "$0")/.."
G=../games
for c in 3 4 5; do
/usr/bin/python3 -W ignore tools/gs_convert.py "$G/DELTARUNE/chapter${c}_windows/data.win" out/deltarune_ch$c deltarune_ch$c > logs/gs_deltarune_ch$c.log 2>&1; echo "exit $?" >> logs/gs_deltarune_ch$c.log
done
echo ALLDONE > logs/gs_all.done
