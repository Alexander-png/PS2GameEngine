# sdk/src - vendored ps2sdk

Source: https://github.com/ps2dev/ps2sdk at commit 39a89923ce59152fa855250cfacaccf8e581a1eb (v2.0.0-119, 2026-08-28), copied
from the maintainer's local checkout (one local change there: .gitignore). License: AFL-3.0 (sdk/src/LICENSE), keep it.
Build outputs go to sdk/ps2sdk (see sdk/build.sh); the engine builds against that, not against /usr/local/ps2dev/ps2sdk.
The cross compilers still come from /usr/local/ps2dev (ee/bin, iop/bin).

## Local changes (keep this list current; candidates for upstreaming)
1. ee/mpeg/src/libmpeg_core.c: D3/D4 QWC swapped in the DMAC handler (pictures > 1023 MBs hang). Upstream PR: https://github.com/ps2dev/ps2sdk/pull/944 (drop our patch 0002 once merged and the pin is moved). Hardware test: tools/mpegtest.
2. Removed (not needed by the engine): ee/network, iop/network, iop/tcpip, iop/fs/bdmfs_fatfs, common/external_deps (lwip/fatfs downloads),
   and the download_dependencies step in the top Makefile.
   also iop/fs/http, iop/fs/netfs (need the removed network stack)
3. install no longer writes into the system toolchain: crt0.o (ee/iop) goes to sdk/ps2sdk/{ee,iop}/startup, ps2dev.cmake to sdk/ps2sdk/share.

## How this is stored in git
`sdk/src` is not committed. `sdk/setup.sh` clones upstream at the pinned commit, removes the unused directories listed in change 2 and applies
`sdk/patches/*.patch` (changes 1 and 3 plus Makefile edits for 2). To change the SDK: edit `sdk/src`, then regenerate the patch for the changed
files with `diff -u` against a clean upstream checkout, and keep the list above current.
