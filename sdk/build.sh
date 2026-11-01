#!/bin/sh
# usage: sdk/build.sh [make args]   builds the vendored ps2sdk (sdk/src) and installs it to sdk/ps2sdk
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
export PS2DEV=${PS2DEV:-/usr/local/ps2dev}          # compilers only
export PS2SDKSRC=$HERE/src PS2SDK=$HERE/ps2sdk
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin
cd "$HERE/src"
make -j${JOBS:-3} "$@" all install
