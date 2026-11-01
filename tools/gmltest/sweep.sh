#!/bin/sh
# sweep.sh <gmltest binary> <gen_dir> <log> : run SCR_GAMESTART + 200 frames in every room, log crashes/timeouts/unimplemented functions
BIN=$(realpath $1); GEN=$2; LOG=$3; : > $LOG
grep -o '^    {"[A-Za-z_0-9]*", [0-9]*, [0-9]*' $GEN/gm_rooms.c | sed 's/^    {"//; s/",.*//' | while read r; do
  rm -rf /tmp/gmsv; mkdir /tmp/gmsv
  out=$( (ulimit -v 2000000; timeout 20 $BIN 200 save=/tmp/gmsv/ run=SCR_GAMESTART roomname=$r hold=40 press=90 period=10 2>&1) ); rc=$?
  [ $rc -ne 0 ] && echo "$r rc=$rc" >> $LOG
  echo "$out" | grep "unimplemented\|unsupported" | sed "s/^/[$r] /" >> $LOG
done
echo finished >> $LOG
