# Unpacking and converting a game

Everything here runs on the PC and needs your own copy of the game. Outputs go to `converter/out/` and logs to
`converter/logs/`; both are git-ignored. Do not commit anything derived from a game.

## One command

```sh
converter/build_game.sh <name> <game_dir> [share_dir]
converter/build_game.sh undertale games/UnderTale
```

`<game_dir>` holds `data.win` (and the external `.ogg` files if the game has any). `share_dir` (default
`/srv/ps2engine`) receives the music `.vas` files. Verified on UNDERTALE v1.0u3: the result is byte-identical to the
pack that runs on the console (about 40 MB, 335 rooms) and takes a few minutes; the 400 MB of music dominates.

## What the steps do

| Step | Script | Output |
|---|---|---|
| texture pages | `winextract.py textures <data.win> <outdir>` (uses `gmqoi`, our own decoder for GameMaker's QOI variant) | `out/<name>/texture_NNN.png` |
| GS assets | `gs_convert.py <data.win> <texdir> <name>` | `out/<name>.gsassets`: every sprite/tileset as PSMT4 or PSMT8 with a CLUT (exact palette when it fits, octree quantization otherwise; error is reported) |
| audio | `vag_convert.py <name> <data.win> <dirs..>` (uses `vagenc`, our SPU-ADPCM encoder) | `out/vag/<name>/{sfx,music}`; SFX mono 22.05 kHz one-shot, music stereo looped streams |
| code | `gml2c.py <data.win> <gen_dir>` | `gm_code_*.c` (GML bytecode to C), `gm_tables.c` (names, strings, function table), `gm_objects.c`, `gm_rooms.c`, `gm_sprites.c` |
| sounds | `gen_sounds.py <name> <data.win> <gen_dir> <share>` | `gm_sounds.c`, `snd.bnk` (SFX bank), copies music to the share |
| pack | `gmpk4.py <data.win> <texdir> <name> <out.gmpk>` | the pack (`GAME.GMP` on the console): rooms, shared asset table, sprite/background/font tables, see [pack_format.md](pack_format.md) |

Everything else in `converter/tools/` is analysis and verification tooling written while exploring the formats:

- `gmwin.py` (data.win chunk reader), `gmroom.py`/`gmroom2.py` (room parsers GMS1 and GMS2), `gmitems.py` (room to draw list with
  GameMaker semantics), `gmrview.py`/`gmdiff.py` (reference renderer and diff against the converted pack), `gmcode.py`/`gmdis.py`/`gmcfg.py`/`gmref.py` (bytecode decoder,
  disassembler, control-flow graph, reference graph), `winscan.py`/`windump.py` (chunk inspection)
- `roompack.py`, `gmpk_pack.py`, `gmpk_sim.py`, `gmpk_rooms.py`, `packplan.py`, `prefetchplan.py`, `roomsets.py`, `roomsnd.py`, `gmroomgraph*.py`: pack planning, prefetch simulation, room graph
- `vid_convert.py` (DELTARUNE videos to MPEG-2 + SPU audio), `audio*.py`, `*_all.sh` (batch runs over all games, they expect games in `games/`)
- `fn.py <gen_dir> <C function>`: which runtime functions a generated function calls (the line offset inside is specific to one `gm_tables.c`; adjust)

## Generated code conventions

- Variable ids are indexes into the `gm_tables.c` name list (id is roughly the file line minus 5); the function table
  `gm_func_names[]` follows. Unknown functions compile to `gm_unsupported("name")` and log at run time, so grep the
  run log for `unsupported`/`unimplemented` to see what to implement next.
- `gm_real_t` is `float` in the PS2 build (`-DGM_REAL_FLOAT`) and `double` on the host: results can differ slightly.
- Bytecode versions: Undertale (GMS1, bytecode 15) works end to end. Undertale Yellow and DELTARUNE use the newer
  bytecode (GMS2, "V2") which `gml2c.py` does not handle yet; the parsers and asset converters already do.

## Adding another game

1. Put it in `games/<name>/`, run `build_game.sh`.
2. Look at the reports in `converter/out/*_gsreport.csv` (texture conversion errors) and the `gml2c` summary
   (functions that failed to translate).
3. Run it on the host harness (building.md), fix missing runtime functions in `engine/core/gml_lib.c`.
   If the game needs a hack, make it data or converter config, never an `if (game == ...)` in the engine.
