# Butterscotch (reference only, AGPL-3.0: do not copy code)
Clone: /home/user/Projects/Butterscotch (shallow, f6f1d30 2026-10-16), also registered as Serena project "Butterscotch" (cpp). Upstream https://github.com/ButterscotchRunner/Butterscotch. Format reference clone: /home/user/Projects/UndertaleModTool (UndertaleModLib has data.win chunk definitions, Underanalyzer GML analysis).
Project goal (user): PS2 engine API-compatible with GameMaker Studio 2 (Undertale / Deltarune): converter unpacks data.win, turns bytecode into C ahead of time, adapts resources, builds .elf. Butterscotch is a bytecode interpreter; it struggles with many moving objects in battles.

## What I found (static read, not yet profiled on PCSX2)
- Platform-independent core in src/: vm.c (4.6k lines, stack-machine interpreter executeLoop at ~2857, switch dispatch), vm_builtins.c (24.6k lines, the GML function library), runner.c (5k, step/draw/events), instance.c, data_win.c (3.4k, chunk parser), collision.h, spatial_grid.c (64 px cells), gml_array.c (refcount + copy-on-write), profiler.c.
- PS2 specific: src/ps2/{main,gs_renderer(3.2k lines),ps2_file_system,ps2_gamepad,ps2_overlay,ps2_utils}.c, src/audio/ps2/ps2_audio_system.c (1.4k, freesd/audsrv IRX).
- PS2 build flags: USE_FLOAT_REALS (GMLReal = float, EE has no double FPU), NO_RVALUE_INT64 (RValue fits 8 bytes), -fsingle-precision-constant, -Werror=double-promotion. Int64 values outside int32 are promoted to real (Undertale mercymod = -99999999999999 is the example). Round-half-even helper GMLReal_bankersRound.
- RValue: tagged union (real/int32/int64/string/array/method/struct/assetref) + type, ownsReference, gmlStackType, assetRefType; 16 bytes with int64, 8 bytes on PS2.
- Instance: built-ins as float fields + per-instance `selfVars` IntRValueHashMap keyed by varID; every GML variable read/write goes through hash lookups (tryFastVarRead). This is exactly what a converter can replace with fixed slots.
- Built-in profilers exist (ENABLE_VM_GML_PROFILER, ENABLE_VM_OPCODE_PROFILER, ENABLE_VM_TRACING, ENABLE_SPATIAL_GRID_LOGS, ENABLE_PS2_RENDERER_LOGS, gprof option): reuse them to find the battle bottleneck.
- Renderer (gs_renderer.c): offline-converted paletted atlases (CLUT4.BIN / CLUT8.BIN, 4bpp/8bpp), VRAM texture cache of 128 KB chunks with LRU eviction and defrag, draws with gsKit_prim_sprite_texture / quad_texture per sprite (CPU builds one GIF packet per prim, no VU1/VIF path), hand-rolled GIF packets only for VRAM->VRAM copies. Candidate improvements for the new engine: batch by atlas, VIF1/VU1 transform, scratchpad.
- Audio uses freesd + audsrv IRX modules (SPU2 stream), see ps2_audio_system.c for how it streams.
- Interpreter details of note: bytecode versions 14/15/16/17 handled (rewriteBytecode14To16, WAD17+ for structs/methods/try-catch), exceptions, static vars, env stack for `with`.

## Next steps (not done)
1. Profile Butterscotch on PCSX2 in a heavy Undertale battle with the built-in profilers to see VM vs draw vs collision share.
2. Write the data.win parser/dumper for the new engine (own code, format from UndertaleModLib) and list the GML builtins/variables each game uses.
3. Runtime skeleton for the new engine.
