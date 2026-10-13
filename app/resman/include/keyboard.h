#ifndef _LW_RM_KEYBOARD_H
#define _LW_RM_KEYBOARD_H
#include "isr.h"
extern volatile DWORD kb_irq_cnt;
extern volatile BYTE kb_last_sc;
extern volatile DWORD kb_dropped;
int resman_keyboard_init(void);
void kb_irq(PISR_FRAME frame);
#endif
