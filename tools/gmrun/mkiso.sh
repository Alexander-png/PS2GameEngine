#!/bin/sh
# usage: mkiso.sh <pack.gmpk>  -> build/gmrun.iso (pack stored as GAME.GMP), for PCSX2
set -e
P=$(realpath "${1:?pack}")
cd "$(dirname "$0")"
rm -rf build/iso; mkdir -p build/iso
cp "$P" build/iso/GAME.GMP
printf 'BOOT2 = cdrom0:\\\\GMRUN.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > build/iso/SYSTEM.CNF
cp gmrun.elf build/iso/GMRUN.ELF
xorriso -as mkisofs -iso-level 1 -V GMRUN -o build/gmrun.iso build/iso >/dev/null 2>&1
ls -la build/gmrun.iso
