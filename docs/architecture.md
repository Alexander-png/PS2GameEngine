# PS2GameEngine architecture (draft 1)

Goal: any GameMaker (GMS1/GMS2) game that fits on a PS2 can be converted and run. Undertale, Undertale Yellow and
DELTARUNE are the first targets and the test suite, not special cases. The repository root is `PS2GameEngine`.

## Layers

```
engine/                      GAME-INDEPENDENT. Knows GameMaker semantics, not any particular game.
  include/ge/                public headers: gfx.h io.h (audio.h input.h save.h to come), room.h
  core/                      platform independent runtime: room/instance/event loop, GML runtime library
                             (builtin functions, ds_*, surfaces, collision, alarms, ...), pack reader, prefetcher
  platform/ps2/              GS gfx, cdvd/HDD io, SPU2 sound server (EE client + IOP irx), IPU video, pad, memory card
  platform/pc/               (later) SDL reference platform so the runtime can be checked without an emulator
converter/                   HOST TOOLS (python/C): data.win reader, GML -> C emitter, asset converters
                             (textures, audio, video, fonts) with per-platform backends (ps2: GS-ready T4/T8, SPU-ADPCM, MPEG-2)
games/<name>/                GAME LAYER: per-game config + generated output. game.toml (target region, resident budget,
                             overrides such as dogcheck/swear-filter data kept as runtime files), generated C from the
                             GML, the .gmpk pack. A game never contains engine code.
tools/                       test programs, emulator scripts (vpad, screenshots), format verifiers
docs/                        pack_format.md, this file, upstream notes
```

## Rules
1. `engine/core` includes only `ge/*.h`; every platform call goes through those interfaces. Adding a platform = a new
   `engine/platform/<name>/` implementing the same headers.
2. The converter outputs *platform-neutral intermediate data first* (rooms, instance lists, sprite metadata, GML IR),
   then a backend writes the platform form. The room format (`.gmr` now, `.gmpk` room blobs next) stores texture
   descriptors (`ge_texdesc_t`: fmt, size, stride, CLUT, pixels), not GS registers, so the core stays generic.
3. GML is compiled ahead of time to C by the converter and linked into the game ELF; game data (rooms, assets,
   moddable text/flags) stays in the pack. Games never reach into platform code; platform code never knows a game.
4. Game-specific behaviour is expressed as converter config or runtime data (games/<name>/), never `if (game == ...)`
   in engine code. Where a game needs a hack the hack lives in its game folder.
5. Same pack serves NTSC and PAL; resolution/scale is a platform decision (`ge_gfx_init`).

## Status (see also docs/status.md)
- engine/core/room.c + engine/platform/ps2/{gfx,io}.c: first cut, verified in PCSX2 with room_ruins1 via tools/roomtest.
- Converter scripts live in converter/tools (see docs/converter.md).
- tools/audiotest/iop/engsnd.c (IOP sound server) moves to engine/platform/ps2/iop/ with the audio interface.
