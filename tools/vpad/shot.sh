#!/bin/sh
# usage: shot.sh <name>   -> .screenshots/<name>.png cropped to the PCSX2 game window (position generous region, the window drifts a little)
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
grim -g "896,352 770x600" "$(cd "$(dirname "$0")/../.." && pwd)/.screenshots/$1.png" && echo "$(cd "$(dirname "$0")/../.." && pwd)/.screenshots/$1.png"
