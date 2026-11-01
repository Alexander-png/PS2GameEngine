# PS2 hardware lessons

Things we learned the hard way. Real hardware (SCPH-39004) and PCSX2 differ; test on the console.
More detail is in `.serena/memories/`.

## Graphics (GS)

- **UV registers are 14 bits (12.4 fixed point).** A source coordinate of texel 1024 (16384) wraps to 0. A sprite whose
  source rectangle ends at texel row/column 1024 is drawn with garbage. `uvfix()` in `engine/platform/ps2/gfx.c`
  clamps UV to 0x3FFF in `ge_gfx_draw_xf`. The host renderer has no such limit, so only the console showed it.
- **RGBAQ state.** Flat (untextured) polygons write raw RGB, textured draws write the 0x80-scaled form. After a flat
  draw the cached colour is stale; `col_ok` forces a rewrite. Without it textured draws came out about 2x too bright.
- Texture height is limited to 1024 texels: large backgrounds are split into 1024-row bands by the packer, tiles are split at band borders.
- VRAM layout: two 150-page framebuffers, a 16-page CLUT region (first CLUT block at page 300), textures from page 316. Textures are
  allocated whole pages; T8 CLUTs are 4 blocks aligned, T4 CLUTs one block. `tbw = (stride + 127) & ~127`.
- Texture upload goes one GS page row at a time (64 rows for T8, 128 for T4); bigger single transfers misbehaved.
- Textures are PSMT4/PSMT8 with a palette; T8 CLUT entries are in CSM1 order (index bits 3 and 4 swapped), alpha 0..0x80.

## CPU

- The EE FPU is single precision only: `gm_real_t` is `float` on the console (`-DGM_REAL_FLOAT`). Avoid `double` in hot paths; it is emulated in software.
- Pointer chasing over all instances is cache-cold. The runtime keeps a first-instance cache and per-target collision candidate lists
  keyed by `inst_epoch`, which any code that creates, destroys or changes the object of an instance must bump (`gml_inst.c`).
- `collision_circle` and `draw_circle` were the hot spots in the slowest room (16 to 29 fps after caching and lookup tables).
- Strings in GML are UTF-8 code points, not bytes (the Russian build depends on it).

## Audio

- SFX in a bank in SPU RAM, music streamed from the HDD by an IOP thread (`engsnd`), two stream slots, ring of two 32 KB halves per channel.
  Loop flags are patched in the ADPCM blocks at run time. Details: `.serena/memories/game_unpacking.md` (audio sections).

## Storage and network

- Boot from HDD (`-DHDD_BOOT`); the deployer syncs files. Logs go to the HDD and are uploaded by the deployer ([deploy.md](deploy.md)).
- SMB1/NTLMv1 only ([smb-share.md](smb-share.md)).
- libmpeg in upstream ps2sdk has the D3/D4 QWC registers swapped in its DMAC handler: pictures over 1023 macroblocks hang. Our patch is in `sdk/patches`; draft upstream issue in `docs/ps2sdk_libmpeg_issue_draft.md`.
