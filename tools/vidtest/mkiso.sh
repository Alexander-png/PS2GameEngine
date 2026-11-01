#!/bin/sh
# usage: mkiso.sh [en|ru|jp]  -> build/vidtest_<lang>.iso (files always named CH3EN/CH5EN so the same ELF plays any variant)
set -e
L=${1:-en}
cd "$(dirname "$0")"
V=../../converter/out/video
rm -rf build/iso; mkdir -p build/iso
for p in "ch3_tenna_intro_$L CH3EN" "ch5_intro_$L CH5EN"; do set -- $p; cp $V/$1.m2v build/iso/$2.M2V; cp $V/$1.vas build/iso/$2.VAS; cp $V/$1.vinfo build/iso/$2.VNF; done
printf 'BOOT2 = cdrom0:\\VTEST.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > build/iso/SYSTEM.CNF
cp vidtest.elf build/iso/VTEST.ELF
xorriso -as mkisofs -iso-level 1 -V VIDTEST -o build/vidtest_$L.iso build/iso >/dev/null 2>&1
ls -la build/vidtest_$L.iso
