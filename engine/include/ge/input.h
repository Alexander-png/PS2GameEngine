/* ge/input.h - platform gamepad. */
#ifndef GE_INPUT_H
#define GE_INPUT_H
enum { GE_BTN_CROSS = 1, GE_BTN_CIRCLE = 2, GE_BTN_SQUARE = 4, GE_BTN_TRIANGLE = 8, GE_BTN_START = 16, GE_BTN_SELECT = 32,
       GE_BTN_UP = 64, GE_BTN_DOWN = 128, GE_BTN_LEFT = 256, GE_BTN_RIGHT = 512, GE_BTN_L1 = 1024, GE_BTN_R1 = 2048 };
int      ge_input_init(void);           /* 0 on success (pad 1 ready), <0 otherwise */
unsigned ge_input_poll(void);           /* bit mask of currently pressed GE_BTN_* (0 if no pad) */
void     ge_input_shutdown(void);       /* close the pad port so padman stops DMA into our buffer (needed before exiting) */
#endif
