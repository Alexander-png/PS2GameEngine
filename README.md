# PS2GameEngine

A PlayStation 2 game engine that is API-compatible with GameMaker. GameMaker games are converted ahead of time on a
PC (GML bytecode to C, textures to GS-ready indexed formats, audio to SPU-ADPCM, rooms to a streamable pack) and run
on the console through a small runtime that implements the GameMaker object/room/event model and the GML library.

Test targets: UNDERTALE 1.0u3, Undertale Yellow, DELTARUNE. They are the test suite, not special cases: game-specific
behaviour never goes into `engine/`. See [docs/status.md](docs/status.md) for what works today (Undertale is playable
from the Ruins to Snowdin on real hardware, an SCPH-39004 FAT).

**This repository contains no game data.** You must own the games and unpack them yourself; nothing extracted from
them (textures, audio, packs, generated C) is committed. See [docs/converter.md](docs/converter.md).

## Layout

| Path | What |
|---|---|
| `engine/` | The runtime. `core/` platform independent (rooms, instances, events, GML library, collision, drawing), `platform/ps2/` (GS graphics, HDD/CD I/O, SPU2 audio client, pad), `platform/host/` (software renderer for tests on the PC), `include/` |
| `converter/` | Host tools (Python + two small C helpers): `data.win` reader, GML bytecode to C (`gml2c.py`), texture / audio / video / room converters, pack writer. `build_game.sh` runs the whole pipeline |
| `tools/gmrun/` | The console program: generic runner for converted games (links the engine and the generated C) |
| `tools/gmltest/` | Host test harness: builds the same runtime for the PC and runs rooms headless with screenshots |
| `tools/deploy/` | Build and stage `BOOT.ELF` + `GAME.GMP` on the SMB share for the console |
| `tools/{audiotest,hwaudio,gsverify,roomtest,vidtest}` | Hardware experiments and format verifiers (IOP sound server, GS texture rules, MPEG video) |
| `tools/vpad/` | Virtual gamepad and screenshot helper for driving PCSX2 |
| `sdk/` | Pinned ps2sdk (fetched and patched by `sdk/setup.sh`, not stored here) |
| `docs/` | Guides and notes (below) |
| `.serena/memories/` | Long-form engineering notes kept as agent memory; useful reading for humans too |

## Quick start

```sh
sdk/setup.sh && sdk/build.sh                 # once: fetch + build the pinned ps2sdk       (docs/setup.md)
mkdir -p games && cp -r "<your>/UnderTale" games/          # your own copy, git-ignored
converter/build_game.sh undertale games/UnderTale          # unpack + convert              (docs/converter.md)
tools/deploy/deploy_gmrun.sh converter/out/undertale.gmpk converter/out/gen_undertale   # build + stage for the console (docs/deploy.md)
```

## Documentation

- [docs/setup.md](docs/setup.md) work environment: packages, toolchain, SDK, optional PCSX2
- [docs/converter.md](docs/converter.md) unpacking and conversion pipeline, formats, what each script does
- [docs/building.md](docs/building.md) building the console program and the host harness
- [docs/deploy.md](docs/deploy.md) the real-hardware loop: PS2Deployer, logs, profiling overlay
- [docs/smb-share.md](docs/smb-share.md) the SMB share the PS2 talks to (Samba config that works with the PS2 network stack)
- [docs/hardware-notes.md](docs/hardware-notes.md) PS2 lessons learned (GS limits, memory, FPU, audio)
- [docs/architecture.md](docs/architecture.md), [docs/pack_format.md](docs/pack_format.md) design
- [docs/status.md](docs/status.md) state of the project and backlog
- [AGENTS.md](AGENTS.md) instructions for AI agents (and a good contributor checklist too)

## License

PS2GameEngine is free software under the **GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)).
Contributions are accepted under the same license.

Reference code: Butterscotch (AGPL) and UndertaleModTool (GPL) were consulted only to understand file formats;
**no code was copied from them**, please keep it that way. `sdk/` fetches ps2dev/ps2sdk (AFL-3.0) at build time and
keeps its own license. UNDERTALE, Undertale Yellow and DELTARUNE belong to their authors; this project ships none of
their content.
