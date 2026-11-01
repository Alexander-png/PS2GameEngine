# Draft: ps2sdk issue / PR — libmpeg: D3/D4 QWC swapped in `_mpeg_dmac_handler`, breaks pictures with > 1023 macroblocks

Status: submitted as https://github.com/ps2dev/ps2sdk/pull/944 (2026-11-01).

Target repo: ps2dev/ps2sdk, file `ee/mpeg/src/libmpeg_core.c` (checked against master at 39a89923, 2026-08-28).

## Summary

`libmpeg` cannot decode MPEG-1/2 video whose pictures contain more than 1023 macroblocks (e.g. 640x480 = 1200 MBs).
The first picture decodes, then the decoder hangs (`_ipu_suspend()` spins forever on `IPU_CTRL & 0xf0`, the IPU output FIFO
never drains) or keeps returning garbage "pictures" without consuming input. Streams with <= 1023 MBs (352x288, 640x368 = 920 MBs, ...) work.
This is probably why the sample and several downstream players describe libmpeg as "reliable only up to about 352x288".

## Root cause

`_MPEG_CSCImage()` converts the decoded picture to RGBA32 in chunks of at most 0x3ff macroblocks. The first chunk is programmed in
`_MPEG_CSCImage()`, the remaining chunks in the DMAC ch3 interrupt handler `_mpeg_dmac_handler()`.
In `_MPEG_CSCImage()` the quadword counts are (correct):

    D4_QWC (toIPU,  macroblocks in)  = mbc * sizeof(_MPEGMacroBlock8) >> 4
    D3_QWC (fromIPU, RGBA32 out)     = mbc * RGBA32_BLOCK_SIZE        >> 4

In `_mpeg_dmac_handler()` the two assignments are swapped:

    *R_EE_D3_QWC  = (mbc * sizeof(_MPEGMacroBlock8)) >> 4;   // wrong: ch3 receives RGBA32
    *R_EE_D4_QWC  = (mbc * RGBA32_BLOCK_SIZE) >> 4;          // wrong: ch4 sends MB8

so for the second chunk the IPU output is under-drained (ch3 takes 24 qw/MB instead of 64 qw/MB) and ch4 feeds 64 qw/MB of
unrelated memory. The IPU is left with data in its output FIFO (`IPU_CTRL.OFC != 0`), which breaks the next picture
(the symptom is `IPU_CTRL & 0xf0` stuck non-zero in `_ipu_suspend()`).

## Fix

```diff
--- a/ee/mpeg/src/libmpeg_core.c
+++ b/ee/mpeg/src/libmpeg_core.c
@@ -262,8 +262,8 @@ s32 _mpeg_dmac_handler(s32 channel, void *arg, void *addr)
     cp->source += mbc * sizeof(_MPEGMacroBlock8);
     cp->dest += mbc * RGBA32_BLOCK_SIZE;
     cp->blocks    = cp->blocks - mbc;
-    *R_EE_D3_QWC  = (mbc * sizeof(_MPEGMacroBlock8)) >> 4;
-    *R_EE_D4_QWC  = (mbc * RGBA32_BLOCK_SIZE) >> 4;
+    *R_EE_D3_QWC  = (mbc * RGBA32_BLOCK_SIZE) >> 4;
+    *R_EE_D4_QWC  = (mbc * sizeof(_MPEGMacroBlock8)) >> 4;
     *R_EE_D4_CHCR = 0x101;
     *R_EE_IPU_CMD = IPU_COMMAND_CSC | mbc;
     *R_EE_D3_CHCR = 0x100;
```

## How it was found / verified (PCSX2 2.x)

* Test stream: MPEG-2 Main, 4:2:0, I/P only, 640x480. Before the fix: frame 1 OK, frame 2 never completes; `IPU_CTRL = 0x02003f86`
  (OFC = 8) while spinning in `_ipu_suspend()`, `D3_QWC = 0`. An all-intra stream shows the same from frame 2/3.
* After the fix: 60/60 frames decoded for all-I 640x480, I/P 640x480 and I/P 320x240; a 40 s 640x480 29.97 fps stream and a
  30 s 640x368 24 fps stream (3.5 Mbps) play to the end with correct picture. Decode ~8 ms/frame in PCSX2.
* Real hardware (SCPH-39004, FAT), 2026-11-01. Test program: `tools/mpegtest` in https://github.com/Alexander-png/PS2GameEngine
  (synthetic ffmpeg `testsrc2` MPEG-2 streams, I/P only, 60 frames, decoded from memory with `MPEG_Initialize/MPEG_Picture`, RGBA32 output, shown on the GS).
  | libmpeg | 352x288 (396 MBs) | 640x480 (1200 MBs) |
  |---|---|---|
  | upstream 39a89923 (unpatched) | OK, 59 pictures, ~3.0 ms/picture | **hangs in the first `MPEG_Picture()`**, never returns (console has to be reset) |
  | with the fix above | OK, 59 pictures, ~3.0 ms/picture | OK, 59 pictures, ~8.3 ms/picture (max 17 ms) |

  The result is the same with and without HDD log writes during decoding. Per-picture average colours of the fixed 640x480 decode match a reference
  ffmpeg decode within 11/255 (same offset as the control stream: colour range conversion), so the pictures are correct, not only complete.
  Note: both streams deliver 59 of their 60 pictures also with the fix (the last picture is not returned), independent of this bug.

## Side note (documentation, optional)

`MPEG_Initialize()` stores the PTS pointer and `MPEG_Picture()` writes through it; passing NULL for either crashes with a
NULL store inside `_get_next_picture()` / `_mpeg12_picture_data()`. The header could state that both pointers must be valid.

## Suggested PR title

`libmpeg: fix swapped D3/D4 QWC in DMAC handler (pictures > 1023 MBs)`
