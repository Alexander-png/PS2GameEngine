#!/bin/sh
# usage: deploy_gmrun.sh <pack.gmpk> <gen_dir> [share_dir]
# Builds gmrun (the converted GameMaker game on the engine; HDD boot, SELECT toggles the profiling overlay, logs to
# hdd0:+DEVGAME:/current/logs), strips it and stages BOOT.ELF, GAME.GMP and manifest.txt for PS2Deployer (default share /srv/ps2engine).
# Extra compile flags: EXTRA_FLAGS="-DSTART_ROOM=6 -DBOOT_SCRIPT=\\\"SCR_GAMESTART\\\" -DOVERLAY_ON" (default: start at room 0, the intro).
set -e
PACK=$(realpath "${1:?usage: deploy_gmrun.sh <pack.gmpk> <gen_dir> [share_dir]}")
GEN=$(realpath "${2:?gen dir}")
DEST=${3:-/srv/ps2engine}
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../../sdk/env.sh"
cd "$HERE/../gmrun"
touch main.c
rm -f main.o log.o
make GEN="$GEN" EXTRA="-DHDD_BOOT $EXTRA_FLAGS" 2>&1 | grep -E "error|undefined" && exit 1
mips64r5900el-ps2-elf-strip -o gmrun_stripped.elf gmrun.elf
# remove stale files of a previous app from the share (PS2Deployer mirrors the manifest)
cp gmrun_stripped.elf "$DEST/BOOT.ELF"
cp "$PACK" "$DEST/GAME.GMP"
python3 "$HERE/mkmanifest.py" "$DEST"
ls -l "$DEST"
