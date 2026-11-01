#!/bin/sh
# usage: run.sh <room_name> <shot_name> [wait_s]   builds a SHOWROOM ELF, makes the ISO, runs it in PCSX2, screenshots into ../../.screenshots


. "$(dirname "$0")/../../sdk/env.sh"
cd "$(dirname "$0")"
touch main.c
[ -n "$FROM" ] && CAMF="-DFROMROOM=\\\"$FROM\\\""
[ -n "$CAM" ] && CAMF="$CAMF -DCAMX=${CAM%,*} -DCAMY=${CAM#*,}"
make EXTRA="$CAMF -DSHOWROOM=\\\"$1\\\"" 2>&1 | grep -E "error|Error"
./mkiso.sh ${PACK:-} >/dev/null
pkill pcsx2-qt; sleep 2
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
(setsid nohup pcsx2-qt -fastboot -batch build/roomtest.iso >/tmp/rt.log 2>&1 </dev/null &)
sleep ${3:-24}
../vpad/shot.sh "$2"
