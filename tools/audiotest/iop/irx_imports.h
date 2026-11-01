#ifndef IRX_IMPORTS_H
#define IRX_IMPORTS_H
#include <irx.h>
#include <libsd.h>
#ifdef ENGSND_IOMANX            /* files via iomanX (needed to reach pfs0:/hdd devices mounted by fileXio) */
#define IOMANX_OLD_NAME_COMPATIBILITY 1
#include <iomanX.h>
#else
#include <ioman.h>
#endif
#include <loadcore.h>
#include <sifcmd.h>
#include <sysmem.h>
#include <sysclib.h>
#include <thbase.h>
#endif
