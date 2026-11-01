# Status

## Works (verified on real hardware unless noted)

- Undertale v1.0u3 (Russian): playable through the Ruins, Snowdin and the bridge area; sound and music; saves; room backgrounds (script driven
  `background_*`; Snowdin Town background verified on the host only).
- Converter: textures, sprites, tilesets, rooms (GMS1 and GMS2 parsers), audio, bytecode to C for GMS1 bytecode; reproducible pack.
- Host harness with a software renderer for headless room checks and screenshots.
- Profiling hooks, log upload loop, SMB deployment.

## Backlog

- Waterfall / Hotland walkthrough (next gameplay milestone).
- Undertale Yellow and DELTARUNE: the newer ("V2") bytecode in `gml2c.py`; GMS2 layers, sequences, shaders; DELTARUNE video playback is verified only in test programs.
- Large arrays in the credits rooms; precise collision masks for GMS2 formats; blend/stretch for dynamic backgrounds.
- Speed: `obj_snowfloor` script; generally any room under 30 fps (check the `draw-prof` lines).
- Tooling: header dependency tracking in `tools/gmrun/Makefile`; log run-number collisions; HDD free-space query; PS2Deployer into this repository.
- Retest by the user: Snowdin Town background and the cleaned release build on the console.
