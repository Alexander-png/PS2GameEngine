#!/bin/sh
# build.sh <gen_dir> <build_dir> : compile generated code + runtime + harness for the host (memory-capped)
set -e
GEN=$(realpath $1); B=$2; ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p $B; cd $B
[ -n "$GFX" ] && { CF_EXTRA="-DGFX"; EXTRA_SRC="$ROOT/engine/core/gml_draw.c $ROOT/engine/core/room.c $ROOT/engine/core/log.c $ROOT/engine/platform/host/io.c $ROOT/engine/platform/host/gfx.c"; }
CF="$GPROF $CF_EXTRA -w -I$ROOT/engine/include -I$GEN"
for f in $ROOT/engine/core/gml_core.c $ROOT/engine/core/gml_inst.c $ROOT/engine/core/gml_lib.c $ROOT/engine/core/gml_file.c $ROOT/engine/core/gml_coll.c $ROOT/engine/core/gml_audio.c $ROOT/tools/gmltest/main.c $EXTRA_SRC $GEN/gm_tables.c $GEN/gm_objects.c $GEN/gm_rooms.c $GEN/gm_sprites.c $GEN/gm_sounds.c; do
  (ulimit -v 3000000; gcc -g -O1 $CF -c $f -o $(basename $f .c).o)
done
ls $GEN/gm_code_*.c | xargs -P4 -I{} sh -c "ulimit -v 3000000; [ \$(basename {} .c).o -nt {} ] || gcc -O0 $CF -c {} -o \$(basename {} .c).o"
gcc $GPROF *.o -lm -o gmltest
