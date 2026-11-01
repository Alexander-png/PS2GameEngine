#!/bin/sh
# usage: extract_all.sh  (run from anywhere; works in converter/) - sequential, one process per data.win
cd "$(dirname "$0")/.."
G=../games
run() { name="$1"; shift; mkdir -p "out/$name"; /usr/bin/python3 tools/winextract.py textures "$1" "out/$name" > "logs/extract_$name.log" 2>&1; echo "exit $?" >> "logs/extract_$name.log"; }
run undertale "$G/UnderTale v1.0u3 Rus/data.win"
[ -f "$G/DELTARUNE/data.win" ] && run deltarune_root "$G/DELTARUNE/data.win"
for c in 1 2 3 4 5; do
  f=$(ls -d "$G"/DELTARUNE/chapter${c}_windows/data.win 2>/dev/null | head -1)
  [ -n "$f" ] && run deltarune_ch$c "$f"
done
echo ALLDONE > logs/extract_all.done
