# Instructions for agents

Read README.md and docs/status.md first. This file is the working agreement; the long-form engineering history is in
`.serena/memories/` (core.md, game_unpacking.md, hardware_docs.md; the other files there are reference notes).

## Hard rules

1. **Never commit game content**: no `data.win`, textures, audio, `.gmpk`/`.GMP`, generated `gm_*.c`, extracted dumps, screenshots of games,
   or anything under `games/`, `converter/out/`, `scratch/`. `.gitignore` covers these; do not `git add -f`. Check `git status` before every commit.
2. **No copied code**: Butterscotch (AGPL) and UndertaleModTool (GPL) are reference reading only. Write our own implementation from the format
   knowledge; do not paste code, comments or tables from them. Same for any other emulator/engine source.
3. **No game-specific code in `engine/`**: per-game behaviour is converter config or runtime data. Games, DELTARUNE and Yellow included, must go through the same engine.
4. **No secrets**: the share password, IPs of personal machines and credentials never go into the repository.
5. Real hardware is the truth. A host or PCSX2 pass is not proof; ask the user to run the console test and read the uploaded logs.
6. Do not fabricate results: say what was and was not tested.

## Environment

- Language C (engine, runtime) and Python 3 (converter). Compilers: `/usr/local/ps2dev`; SDK: vendored, `. sdk/env.sh` in every new shell (variables are not persistent).
- In the Serena MCP shell: if it reports "No active project", call `activate_project PS2GameEngine`. Calls time out at about 60 s: run long jobs detached
  (`(setsid nohup <cmd> > /tmp/x.log 2>&1 &)`) and poll the log. The Serena shell and the cloud sandbox are different machines: write project files
  through Serena (`create_text_file` or shell heredocs), not with a local Write tool.
- Pillow is available in `/usr/bin/python3` on the dev machine; numpy is not.
- Images: convert to PNG, copy into a connected folder and stage with `device_stage_files` before viewing.

## Workflow

- Convert with `converter/build_game.sh`, check on the host harness (`docs/building.md`), deploy with `tools/deploy/deploy_gmrun.sh`, have the user run
  PS2Deployer, play, run it again, then read `/srv/ps2engine/logs/*`. Logs are split per area; the newest `gmrun-rNNN-*.txt` is the latest run.
- After changing a struct in a header, delete `tools/gmrun/*.o` except the three `*_irx.o` (no header dependency tracking).
- Performance work: use the `draw-prof` lines first, then optimise; keep the semantic caches correct (`inst_epoch`, see hardware-notes.md).
- Keep debug code out of commits (temporary logging, inspectors). Use the profiler hooks and env-var debug of the host renderer instead.
- GS rules to remember: 14-bit UVs, RGBAQ state after flat draws, 1024 texel height limit, `float` only on the EE.
- Record durable findings: update `docs/hardware-notes.md`, `docs/status.md` and `sdk/UPSTREAM.md` when relevant; add detailed history to `.serena/memories/`.

## Code style

- C99, 4-space indent, short functions, comments explain why. The runtime must stay single-threaded and allocation-light in the frame loop.
- Python: standard library + Pillow only. Scripts take explicit paths; defaults are relative to the repository.
- Every user-facing change gets a line in docs when it changes a workflow.

## Commits

- Small logical commits with an imperative subject and a body that says why. Run no destructive git commands (reset --hard, force push) unless the owner asks.
- Agent-authored commits end with the trailer lines the harness provides (Co-Authored-By and session link).
- Do not push or create remotes unless asked. No LICENSE has been chosen yet; do not add one without the owner's decision.

## Where things are

See the table in README.md. Entry points: `engine/core/gml_inst.c` (instances, events, draw loop, profiler), `gml_lib.c` (builtin GML functions),
`gml_coll.c` (collision), `gml_draw.c` (draw functions), `room.c` (rooms, tile/background drawing), `platform/ps2/gfx.c` (GS), `converter/tools/gml2c.py` (code generator), `tools/gmrun/main.c` (console main loop).
