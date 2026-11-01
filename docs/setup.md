# Work environment

The project was developed on Arch Linux (a VirtualBox VM). Anything similar works.

## 1. Packages

```sh
sudo pacman -S --needed base-devel git python python-pillow samba
# optional: emulator + virtual gamepad + screenshots
sudo pacman -S --needed pcsx2 python-evdev grim ffmpeg
```

Python 3.11+ and Pillow are all the converter needs. The converter has no other Python dependencies.

## 2. PS2 toolchain (ps2dev)

Install the cross compilers from <https://github.com/ps2dev/ps2dev> (its README has the one-shot script) into
`/usr/local/ps2dev` (`PS2DEV=/usr/local/ps2dev`). You need `ee/` and `iop/` compilers (`mips64r5900el-ps2-elf-gcc`,
`mipsel-ps2-irx-gcc`) and `gsKit` (`$PS2DEV/gsKit`). The toolchain's own ps2sdk is only used for PS2Deployer (see
[deploy.md](deploy.md)); the engine builds against the vendored one below.

## 3. The pinned SDK

```sh
sdk/setup.sh        # clones ps2dev/ps2sdk at the pinned commit into sdk/src and applies sdk/patches/*.patch
sdk/build.sh        # builds and installs to sdk/ps2sdk
. sdk/env.sh        # in every new shell: PS2DEV, PS2SDK, GSKIT, PATH
```

`sdk/UPSTREAM.md` lists what we changed against upstream and why (libmpeg DMAC fix, no network stack, installs kept
out of the system toolchain). If you change the SDK, regenerate the patches from a clean upstream checkout and keep
that list current.

## 4. Games

Put your own copies under `games/<name>/` (git-ignored). The converter reads `data.win` and the external audio files.
Note: `games/` and `converter/out/` are in `.gitignore`; never force-add anything from them.

## 5. Host check

```sh
GFX=1 tools/gmltest/build.sh converter/out/gen_undertale build-host    # needs a converted game, see converter.md
```

## 6. Optional: PCSX2

PCSX2 is good for quick checks of graphics and logic but **real hardware is the reference** (several bugs only showed
on the console, see hardware-notes.md). `tools/vpad/vpad.py` creates a uinput gamepad (`daemon` + `send "tap cross"`);
`tools/vpad/shot.sh` crops a Wayland screenshot of the emulator window (edit the region for your layout).
