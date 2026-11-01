# GMPK: streaming pack format (draft 1)

Status: design draft derived from the measurements in the converter output (room graph, per-room asset sets,
audio sizing). Numbers marked **(assumed)** must be measured on hardware/PCSX2 before they are frozen.

## 1. Goals and constraints

- Runs from DVD or internal HDD first (USB later), PS2 FAT, NTSC and PAL from the **same data**.
- Loads must be hidden behind fades/prefetch; average transition delta measured at 0.05-0.5 MB (Yellow up to ~1 MB).
- Sprite data is stored GS-ready (swizzled, palettised) so the load path is read -> DMA, no CPU conversion.
- Disc space is cheap (DVD 4.7 GB, HDD tens of GB): duplicate data to avoid seeks. Optimise for **number of reads**,
  not size. Measured duplication of per-room records: x3-4 over unique assets.
- Music is streamed, never resident. Short SFX live in SPU2 RAM (2 MB total, shared with reverb/stream buffers).

## 2. Container

One file `GAME.GMPK` (one per game/chapter) placed first on the disc; everything is 2048-byte sector aligned so any
record can be fetched with one contiguous sector read (`LBA = file_lba + offset/2048`).

```
GmpkHeader            (sector 0)
  char  magic[4]      "GMPK"
  u32   version
  u32   toc_off, toc_count          section table
  u32   boot_off, boot_len          boot pack (always loaded first, sequential)
  u32   flags                       bit0: LE, bit1: has frame-streams, bit2: has music index
Section TOC entry:    u32 kind, u32 off (sectors), u32 len (sectors), u32 count
kinds: 1 BOOT, 2 ROOM_INDEX, 3 ROOM_DATA, 4 ASSET_TABLE, 5 FRAME_STREAM, 6 SFX_POOL, 7 MUSIC_INDEX, 8 MUSIC_DATA, 9 CODE_TABLES
```

Section order on disc = expected read order: BOOT, ROOM_INDEX, ASSET_TABLE, ROOM_DATA (rooms ordered by game
progress / graph BFS from the start room so neighbours are physically close), SFX_POOL, FRAME_STREAM, MUSIC_*.

## 3. Boot pack (resident core)

Loaded once, kept in EE RAM, never evicted:

- ROOM_INDEX, ASSET_TABLE (metadata only), object/sprite/font tables, GML constant tables, string table
- **Resident assets**: every asset used by >= 5-10% of rooms (HUD, text box, fonts, player, common UI, common SFX
  bank). Measured size: UT 1.1 MB, Yellow ~2.9 MB, DELTARUNE ch2 ~4.2 MB, ch4 ~2.5 MB.
- Dog-check room (`PLACE_DOG` / `PROCESS_DOG`, `dogcheck` music): always resident. Reached only from tampered saves,
  never prefetched, but must always load.
- Dog-check whitelist arrays and swear-filter lists stay as **runtime data** (tables in CODE_TABLES), never folded.

## 4. Assets

```
struct AssetEntry {        // ASSET_TABLE, indexed by asset id (sprite/tileset/background share one id space)
  u32 id;  u8 kind;        // 0 sprite, 1 tileset, 2 bg, 3 sfx, 4 font
  u8  psm;                 // GS pixel format: CLUT4 / CLUT8 / RGBA16 / RGBA32
  u16 frames;
  u32 data_off_in_room[?]; // see below: location is per room record, not here
  u32 vram_bytes;          // size after upload (256-byte blocks)
  u32 ee_bytes;            // size in EE RAM
}
```

Texture payload per frame: `{u16 w,h, u16 x_origin,y_origin, u16 tbw, u8 psm, u8 flags}`, then CLUT (64 B / 1 KB,
already in GS CSM1 order), then the image in GS block order (swizzled). Large atlases are split into <= 256x256
tiles at conversion time so GS texture-cache behaviour is predictable.

**GS facts verified in PCSX2 (tools/gsverify, 2026-10-17)**, with real converted Undertale sprites:
- PSMT4 data: first pixel in the **low nibble**; PSMT8: one byte per pixel; both uploaded as a linear raster with
  row stride padded to whole quadwords (32 px for T4, 16 px for T8). Works with `draw_texture_transfer`
  (needs `dma_channel_send_chain`; the helper `draw_texture_flush` ends the chain, nothing may follow it).
- CLUT8 in CSM1 order = logical index with bits 3 and 4 swapped (same as the GS manual's 16x16 layout and the
  gsKit CSM1 example); CLUT4 is plain order (8x2). Uploaded as PSMCT32 rects (16x16 / 8x2), CLUT alpha 0x80 = opaque,
  alpha blend `(Cs-Cd)*As+Cd` composites correctly.
- **Buffer width (TBW) of PSMT4/PSMT8 textures must be a multiple of 128 px (even TBW)**: with TBW = 192 the T8 image
  was sheared (page rows overlap); with 256 it rendered correctly. So `tbw_px = ceil(stride/128)*128`, derived from the
  stored stride, and VRAM cost = `tbw_px * h * bpp` (earlier VRAM estimates used width*h and are up to ~1.3x low).
- Not verified: block-swizzled "upload as PSMCT32" optimisation; TEXA/alpha-test variants; RGBA16/32 paths.

Sprites that do not fit CLUT8 (about 3-10% per game) are stored RGBA16 (or RGBA32 for gradients) with the dither
chosen at conversion time.

## 5. Room records and the delta rule

```
struct RoomIndexEntry {
  u32 room_id;
  u32 data_off, data_len;          // contiguous blob in ROOM_DATA (sector aligned)
  u32 kb_ee, kb_vram;              // working-set sizes for the budgeter
  u16 n_assets, n_sfx, n_succ;
  u32 assets_off;                  // array of AssetRef in the blob header
  u32 succ_off;                    // array of Successor, sorted by priority
  u32 flags;                       // bit0 has_dynamic_exits, bit1 fade_in, bit2 big (needs frame streaming)
}
struct AssetRef  { u32 asset_id; u32 off_in_blob; u32 len; }
struct Successor { u32 room_id; u8 kind; u8 prio; u16 hint_var; } // kind: const / instance-var / next / prev / dog
```

The blob for a room holds **all non-resident assets of that room**, stored contiguously (duplicated across rooms).

Runtime loading rule, given the set `R` of assets already resident (refcount > 0):

1. Compute `missing = assets(B) - R`.
2. If `bytes(missing) >= 50% of blob` -> one sequential read of the whole blob; keep only the missing assets.
3. Otherwise issue reads for the missing `AssetRef`s sorted by offset; merge gaps <= 64 KB into one read.

This keeps the average transition at the measured delta (tens to hundreds of KB) while the worst case is one
sequential read. Rule parameters are tunable per medium (DVD seek is expensive, HDD less).

## 6. Prefetch

- On room enter: walk `Successor` list; for each, if its estimated delta fits in the free prefetch budget
  (default 2-4 MB, set by memory plan), queue it. Smallest delta first, `dog` successors never queued.
- Rooms with `has_dynamic_exits` (unresolved `room_goto(<expr>)`): engine inspects the target variable of the nearby
  door-like instances (`hint_var` names it, extracted by the converter) and prefetches that room.
- Profile mode (host build): records real transitions, merged into a priority table shipped in the next pack.
- Prefetch is cancelled/evicted (LRU by refcount 0) when memory is needed; never blocks the game thread.
- Measured coverage (static graph, 2 MB prefetch budget): 78-100% of known edges fully prefetchable
  (Yellow worst), 91-100% at 4 MB.

## 7. Huge scenes: frame streams

Rooms flagged `big` (Flowey boss ~13 MB in UT/Yellow, Yellow intro/config ~6 MB) never keep every frame resident.
`FRAME_STREAM` stores such sprites as a sequence of frame records in play order, each independently uploadable:

```
struct FrameRec { u32 next_off; u16 w,h; u8 psm,flags; u32 clut_off; u32 data_len; }
```

The engine keeps a ring of N frames ahead of `image_index` (N derived from `image_speed`, default 3-6 frames),
uploaded to a VRAM staging area. Non-animated usage (static `draw_sprite` of a single frame) loads only that frame.

## 8. Audio

### SFX
- Converted offline to SPU2 ADPCM (VAG), mono, 22.05 kHz default (per-sound override for important sounds), loop
  flags set in the last block for looping sounds.
- Per-room SFX list in the room record (`n_sfx` entries, sfx ids). Resident SFX bank in boot pack for common sounds.
- SPU2 RAM is managed by a block allocator with refcounts; per-room SFX working sets measured at 40-830 KB (median
  40-480 KB), two outliers above 1.5 MB. Sounds that do not fit are streamed from the IOP buffer on demand.

### Music (primary: SPU2 ADPCM streaming)
- Each track stored as interleaved stereo VAG in `MUSIC_DATA`, 2 KB chunks aligned, with exact loop start/end block
  in `MUSIC_INDEX`. No decode CPU, loop exact at block level.
- Size: UT ~400 MB, Yellow ~470 MB, DELTARUNE union ~930 MB (640 MB at 32 kHz) -> fits DVD or HDD.
- Streaming: IOP reads 2 x 64 KB per active stream (1-2 streams; crossfade needs two), feeding SPU2 via DMA.
  Bandwidth ~50 kB/s per stereo stream.

### Music (optional: Ogg Vorbis)
- Kept as build option (`MUSIC_CODEC=ogg`) for space/quality. Decoder is Tremor-class integer (preferred, EE has
  64-bit GPRs so 32x32->64 multiply is one `dmult`; MMI `PMADDW/PMULTW` can accelerate IMDCT butterflies and
  windowing/overlap-add) or a float decoder using single-precision code and VU0 macro mode for the vector parts.
  The EE FPU is single precision only, so the float path must avoid doubles.
- To decide: profile both on real hardware/PCSX2 (PCSX2 timing is not cycle-accurate) with a 44.1 kHz stereo
  stream of ~150 kbps; criteria: CPU share per stream, artifacts vs reference decode, memory.

## 9. NTSC / PAL

Pack contents are region independent. Differences live in the engine only:
- video mode (NTSC 640x448 / PAL 640x512 interlaced or progressive variants), framebuffer allocation (leaves
  ~70-80 KB less VRAM on PAL for textures, to be computed in the VRAM plan);
- game logic ticks at the GameMaker `room_speed` through a fixed timestep (30/60 steps/s) decoupled from vsync
  (59.94 vs 50 Hz): pacing, animation speed and audio sync use the step clock, not vsync counts;
- audio rates and streaming budgets identical.

## 10. Medium budgets (assumed, to be measured)

| Medium | Sustained | Seek | Notes |
|---|---|---|---|
| DVD | ~4-8 MB/s | 100-200 ms | one big read beats many small ones; place hot sections together |
| HDD (ATA via IOP) | ~15-25 MB/s | ~10 ms | per-asset reads cheap; room blobs still preferred |
| USB (later) | ~1-5 MB/s | low | needs smaller prefetch units |

Typical transition (0.05-0.5 MB) = 10-150 ms transfer + one seek, inside a fade. Worst room blob (~4-5 MB)
needs prefetch or a longer fade.

## 11. Converter output (new tools to write)

1. `packer`: reads `roomsets` (sprites + code refs), `roomgraph2`, `audio` CSVs -> builds resident set, room blobs,
   successor lists, frame streams, SFX banks, music index; emits `GAME.GMPK` + a C header of ids.
2. `gs_convert`: re-atlas, palettise (CLUT4/8), GS swizzle, dither.
3. `vag_convert`: PCM/OGG -> VAG 22.05 kHz mono (SFX) and stereo (music), loop flags from GameMaker loop points.
4. Host profiler build to record real room transitions for the next pack.
