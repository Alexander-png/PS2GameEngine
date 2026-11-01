# Building

All commands from the repository root, after `. sdk/env.sh` (see [setup.md](setup.md)).

## Console program (gmrun)

`tools/gmrun` is the generic runner: engine core + `platform/ps2` + the game's generated C.

```sh
make -C tools/gmrun GEN=../../converter/out/gen_undertale EXTRA="-DHDD_BOOT"
```

Usually you use the deploy wrapper, which also strips the ELF and stages it ([deploy.md](deploy.md)).
Useful `EXTRA` defines: `-DHDD_BOOT` (boot from HDD, log to `hdd0:+DEVGAME:/current/logs`), `-DSTART_ROOM=<n>`,
`-DBOOT_SCRIPT=\"SCR_GAMESTART\"`, `-DOVERLAY_ON` (profiling overlay; SELECT toggles it at run time).

**Gotcha:** the Makefile has no header dependency tracking. After changing a struct in a header under `engine/include`,
delete the objects: `rm tools/gmrun/*.o` (keep `fileXio_irx.o`, `ps2hdd_irx.o`, `ps2fs_irx.o`, they are slow to regenerate).
The generated `gm_code_*.c` are compiled with `-Os`, the runtime with `-O2`.

## Host harness (gmltest)

The same runtime built for the PC with a software renderer (`engine/platform/host`). Use it first for logic and
rendering checks; it is quick. It has no 14-bit UV limit and double-precision reals, so some bugs only show on the console.

```sh
GFX=1 tools/gmltest/build.sh converter/out/gen_undertale build-host     # GFX=1: software renderer; GPROF=-pg: gprof build
cd build-host
./gmltest 8 pack=../converter/out/undertale.gmpk run=SCR_GAMESTART roomname=room_ruins1 step=2 \
          roomname=room_tundra6A step=3 shot=6:/tmp/x.ppm
```

Arguments are processed in order; room names are case-sensitive. `tools/gmltest/sweep.sh <gmltest> <gen_dir> <log>`
runs `SCR_GAMESTART` and 200 frames in every room and logs crashes, timeouts and unimplemented functions.
Debug environment variables for the host renderer: `GT_LIST`, `GT_STAT`, `GT_BIG`, `GT_TEXDBG`, `GT_DRAWDBG`.

## Profiling

`gm_prof_*` (in `engine/core/gml_inst.c`) times draw events per object plus a few hot builtins. On the console
`tools/gmrun/main.c` prints `draw-prof` lines to the log every 5 s with the EE cycle counter; use it when a room is slow.

## Other test programs

`tools/roomtest`, `vidtest`, `audiotest`, `hwaudio`, `gsverify` are standalone ELFs (`make` in each directory, most have a `mkiso.sh`
to make a test ISO for PCSX2). `gsverify` checks the GS texture formats against reference PNGs; `hwaudio` exercises the IOP sound server
(`engsnd`) on real hardware.
