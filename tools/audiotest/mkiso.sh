#!/bin/sh
# builds build/audiotest.iso: SYSTEM.CNF + ATEST.ELF + the music test file
set -e
cd "$(dirname "$0")"
V=../../converter/out/vag/undertale/music
rm -rf build; mkdir -p build/iso
printf 'BOOT2 = cdrom0:\\ATEST.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > build/iso/SYSTEM.CNF
cp audiotest.elf build/iso/ATEST.ELF
cp $V/mus_xpart.vas build/iso/XPART.VAS
cp $V/mus_menu0.vas build/iso/MENU.VAS
cp $V/mus_zzz_c.vas build/iso/ZZZC.VAS
cp $V/mus_prebattle1.vas build/iso/PRE.VAS
cp $V/mus_undynepiano.vas build/iso/PIANO.VAS
R=../../converter/out/vag
cp $R/deltarune_ch2/music/cyber_battle_backing.vas build/iso/CBACK.VAS
cp $R/deltarune_mus/cyber_battle_prelude.vas build/iso/CPRE.VAS
xorriso -as mkisofs -iso-level 1 -V AUDIOTEST -o build/audiotest.iso build/iso >/dev/null 2>&1
ls -la build/audiotest.iso
