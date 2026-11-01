#!/bin/sh
# Builds libmpeg WITHOUT our fix (patch 0002 reversed) as build/orig/libmpegorig.a, for the "before" run of mpegtest.
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
. "$ROOT/sdk/env.sh"
rm -rf build/orig_tmp build/orig
mkdir -p build/orig build/orig_tmp
cp -a "$ROOT/sdk/src/ee/mpeg" build/orig_tmp/mpeg      # the directory must be called mpeg (the library name comes from it)
rm -rf build/orig_tmp/mpeg/lib build/orig_tmp/mpeg/obj
(cd build/orig_tmp/mpeg && patch -R -p3 < "$ROOT/sdk/patches/0002-libmpeg-dmac-qwc-swap.patch")
PS2SDKSRC="$ROOT/sdk/src" make -C build/orig_tmp/mpeg >/dev/null
cp build/orig_tmp/mpeg/lib/libmpeg.a build/orig/libmpegorig.a
echo built build/orig/libmpegorig.a
