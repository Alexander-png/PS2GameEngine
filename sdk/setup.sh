#!/bin/sh
# sdk/setup.sh: fetch ps2dev/ps2sdk at the pinned commit into sdk/src and apply our patches (sdk/patches/*.patch).
# Afterwards build + install the SDK with sdk/build.sh (installs to sdk/ps2sdk). See sdk/UPSTREAM.md.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
COMMIT=39a89923ce59152fa855250cfacaccf8e581a1eb
[ -e "$HERE/src" ] && { echo "sdk/src exists; remove it first to re-run"; exit 1; }
git clone https://github.com/ps2dev/ps2sdk "$HERE/src"
cd "$HERE/src"
git checkout -q "$COMMIT"
rm -rf .git
# parts of the SDK the engine does not use (network stack and friends; they also download external sources at build time)
rm -rf common/external_deps ee/network iop/network iop/tcpip iop/fs/bdmfs_fatfs iop/fs/http iop/fs/netfs
for p in "$HERE"/patches/*.patch; do echo "applying $(basename "$p")"; patch -p1 < "$p"; done
echo "done; now: sdk/build.sh"
