#!/bin/sh
# usage: mkiso.sh [pack.gmpk]  -> build/roomtest.iso (pack stored as GAME.GMP)
set -e
P=${1:-../../converter/out/undertale_rooms.gmpk}
[ -z "$1" ] && P=../../converter/out/undertale_rooms.gmpk
cd "$(dirname "$0")"
rm -rf build/iso; mkdir -p build/iso
cp "$P" build/iso/GAME.GMP
printf 'BOOT2 = cdrom0:\\\\RTEST.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > build/iso/SYSTEM.CNF
cp roomtest.elf build/iso/RTEST.ELF
xorriso -as mkisofs -iso-level 1 -V ROOMTEST -o build/roomtest.iso build/iso >/dev/null 2>&1
ls -la build/roomtest.iso
