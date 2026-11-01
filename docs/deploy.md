# Real hardware loop

Target: PS2 FAT (SCPH-39004) with a network adapter and HDD. The console fetches `BOOT.ELF`, `GAME.GMP` and music files from an SMB share on
the development machine ([smb-share.md](smb-share.md)) with **PS2Deployer**, stores them on the HDD
(`hdd0:+DEVGAME:/current`) and runs them. Logs are written to the HDD and uploaded back on the next deployer run.

## One iteration

1. On the PC, from the repository root:
   ```sh
   (setsid nohup sh tools/deploy/deploy_gmrun.sh converter/out/undertale.gmpk converter/out/gen_undertale > /tmp/dep.log 2>&1 &)
   ```
   It builds gmrun (about 40 s incremental), strips the ELF and stages `BOOT.ELF`, `GAME.GMP`, `manifest.txt` in
   `/srv/ps2engine` (`mkmanifest.py` lists the files and sizes; it skips `*.vas` unless `--all`, so the music is only
   re-sent when it changes). Check `/tmp/dep.log` for `error|undefined`.
   `EXTRA_FLAGS` passes defines, e.g. `EXTRA_FLAGS="-DSTART_ROOM=6 -DOVERLAY_ON"`.
2. On the PS2: run **PS2Deployer**. It mirrors the manifest into `hdd0:+DEVGAME:/current` (only changed files are transferred).
3. Launch `current/BOOT.ELF` from the HDD launcher, play, quit.
4. Run **PS2Deployer** again: it first uploads the logs to `/srv/ps2engine/logs/` (`gmrun-rNNN-<area>.txt`, split by area, then renamed `.sent` on the console), then verifies files.
5. Read the log on the PC (`grep -a "fps\|draw-prof\|unsupported" /srv/ps2engine/logs/<file>`).

Tip: the serial shell tool times out around 60 s, so run the deploy detached as shown and poll.

## PS2Deployer

A separate project (not part of this repository yet): a small PS2 program that talks SMB to the share and syncs the files.
It is built with the **system** ps2sdk, because the vendored SDK has no network modules:

```sh
make BUILD=build-engine SHARE=ps2engine SERVER=<ip of the dev machine>
```

The server address, share name and credentials are compile-time macros. The share user is `ps2deploy`; its password
is not in this repository. Put the VM on a **bridged** network adapter, otherwise the console cannot reach it.

## Reading logs

- Each 5 s: `fps` plus `draw-prof` lines (top objects by draw time, `fn:draw_circle`, `fn:collision_circle`, `fn:gm_obj1`).
- `unsupported ...` / `unimplemented ...`: a GML function the runtime lacks.
- SELECT toggles an on-screen overlay (needs `-DOVERLAY_ON` at build time to start enabled).
