#!/bin/sh
# usage: deploy.sh <pack.gmpk> [share_dir]
# Builds the HDD benchmark (roomtest, prefetch on, HDD boot) and stages it for PS2Deployer:
#   <share_dir>/BOOT.ELF, <share_dir>/GAME.GMP, <share_dir>/manifest.txt   (default share_dir /srv/ps2engine)
# PS2Deployer (built with SHARE=ps2engine) copies them to hdd0:+DEVGAME:pfs:/current and uploads logs back to <share_dir>/logs.
set -e
PACK=$(realpath "${1:?usage: deploy.sh <pack.gmpk> [share_dir]}")
DEST=${2:-/srv/ps2engine}
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../../sdk/env.sh"
cd "$HERE/../roomtest"
touch main.c
make clean >/dev/null 2>&1 || true
make EXTRA="-DPREFETCH -DHDD_BOOT" 2>&1 | grep -E "error|undefined" && exit 1
cp roomtest.elf "$DEST/BOOT.ELF"
cp "$PACK" "$DEST/GAME.GMP"
python3 "$HERE/mkmanifest.py" "$DEST"
ls -l "$DEST"
