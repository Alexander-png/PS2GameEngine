#!/bin/sh
# build_game.sh <name> <game_dir> [share_dir]
#
# Whole conversion pipeline for one GameMaker game (data.win -> pack + generated C), run from anywhere.
#   <name>      short id used for output files, e.g. undertale
#   <game_dir>  folder that holds data.win (and the external audio files, if any)
#   [share_dir] where the music .vas files are copied (default /srv/ps2engine, the PS2Deployer share)
#
# Results (all under converter/out, git-ignored):
#   out/<name>/            decoded texture pages          (winextract.py)
#   out/<name>.gsassets    GS-ready sprites / tilesets    (gs_convert.py)
#   out/vag/<name>/        SPU-ADPCM audio                (vag_convert.py)
#   out/gen_<name>/        generated C (gm_code_*.c, gm_tables.c, ...) + gm_sounds.c   (gml2c.py, gen_sounds.py)
#   out/<name>.gmpk        the pack the console loads as GAME.GMP                      (gmpk4.py)
# Then deploy with tools/deploy/deploy_gmrun.sh converter/out/<name>.gmpk converter/out/gen_<name>.
# Steps already done are not skipped; delete the output to force a rebuild of a step.
set -e
NAME=${1:?usage: build_game.sh <name> <game_dir> [share_dir]}
GAME=$(realpath "${2:?game dir with data.win}")
SHARE=${3:-/srv/ps2engine}
cd "$(dirname "$0")"
mkdir -p out logs
T=tools
WIN="$GAME/data.win"
[ -f "$WIN" ] || { echo "no data.win in $GAME"; exit 1; }
PY=${PYTHON:-python3}

# the two tiny C helpers (GM-QOI texture decoder, SPU-ADPCM encoder) are called by the scripts as tools/gmqoi, tools/vagenc
[ -x $T/gmqoi ] || cc -O2 -o $T/gmqoi $T/gmqoi.c
[ -x $T/vagenc ] || cc -O2 -o $T/vagenc $T/vagenc.c -lm

echo "== textures";  mkdir -p out/$NAME; $PY $T/winextract.py textures "$WIN" out/$NAME
echo "== GS assets"; $PY -W ignore $T/gs_convert.py "$WIN" out/$NAME $NAME
echo "== audio";     $PY $T/vag_convert.py $NAME "$WIN" "$GAME"
echo "== code";      $PY $T/gml2c.py "$WIN" out/gen_$NAME
echo "== sounds";    $PY $T/gen_sounds.py $NAME "$WIN" out/gen_$NAME "$SHARE"
echo "== pack";      $PY $T/gmpk4.py "$WIN" out/$NAME $NAME out/$NAME.gmpk
ls -l out/$NAME.gmpk
