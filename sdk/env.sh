# source this: . sdk/env.sh   -> builds use the vendored SDK (sdk/ps2sdk), compilers from /usr/local/ps2dev
_HERE=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)
export PS2DEV=${PS2DEV:-/usr/local/ps2dev}
export PS2SDK=$_HERE/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH=$PS2SDK/bin:$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin
