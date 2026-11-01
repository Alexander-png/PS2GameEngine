# mpegtest

Hardware test for the ps2sdk libmpeg fix (`sdk/patches/0002`): decodes a 352x288 control stream and a 640x480 stream (1200 macroblocks, the case that
broke) from synthetic ffmpeg-generated MPEG-2 data, logs per-frame average colour and timing to the HDD log folder.

    tools/mpegtest/gen_streams.sh          # needs ffmpeg
    . sdk/env.sh && make -C tools/mpegtest # mpegtest.elf (with the fix)
    tools/mpegtest/build_orig_lib.sh && make -C tools/mpegtest ORIG=1   # mpegtest_orig.elf (unpatched upstream library; delete main.o between the two builds)

Stage the stripped ELF as `BOOT.ELF` on the share (see docs/deploy.md), run it, then upload the log with PS2Deployer.
Result 2026-11-01 (fixed library, real PS2): both streams decode completely, 59 of 60 pictures each.
