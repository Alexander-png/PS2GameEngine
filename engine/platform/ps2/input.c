/* PS2 gamepad via libpad (rom0 SIO2MAN/PADMAN). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include "ge/input.h"

static char padbuf[256] __attribute__((aligned(64)));
static int ready;

int ge_input_init(void)
{
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);      /* harmless when a launcher already loaded them */
    SifLoadModule("rom0:PADMAN", 0, NULL);
    if (padInit(0) < 0) return -1;
    if (!padPortOpen(0, 0, padbuf)) return -2;
    for (int i = 0; i < 200; i++) {              /* wait until the pad reports a stable state */
        int st = padGetState(0, 0);
        if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) { ready = 1; return 0; }
        usleep(10000);
    }
    return -3;
}

unsigned ge_input_poll(void)
{
    struct padButtonStatus b;
    if (!ready || padGetState(0, 0) != PAD_STATE_STABLE || !padRead(0, 0, &b)) return 0;
    unsigned p = 0xFFFF ^ b.btns, r = 0;
    if (p & PAD_CROSS) r |= GE_BTN_CROSS;     if (p & PAD_CIRCLE) r |= GE_BTN_CIRCLE;
    if (p & PAD_SQUARE) r |= GE_BTN_SQUARE;   if (p & PAD_TRIANGLE) r |= GE_BTN_TRIANGLE;
    if (p & PAD_START) r |= GE_BTN_START;     if (p & PAD_SELECT) r |= GE_BTN_SELECT;
    if (p & PAD_UP) r |= GE_BTN_UP;           if (p & PAD_DOWN) r |= GE_BTN_DOWN;
    if (p & PAD_LEFT) r |= GE_BTN_LEFT;       if (p & PAD_RIGHT) r |= GE_BTN_RIGHT;
    if (p & PAD_L1) r |= GE_BTN_L1;           if (p & PAD_R1) r |= GE_BTN_R1;
    return r;
}

void ge_input_shutdown(void)
{
    if (!ready) return;
    ready = 0;
    padPortClose(0, 0);
    padEnd();
}
