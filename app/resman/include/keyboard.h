#ifndef _LW_RM_KEYBOARD_H
#define _LW_RM_KEYBOARD_H
#include "isr.h"
#include "../../../lib/include/resman.h"
int resman_enable_input(void);
void resman_input_wait(void);
int resman_getmpoll(void);
int resman_getmpos(RM_MOUSE_POS *pos);
extern volatile DWORD kb_irq_cnt;
extern volatile BYTE kb_last_sc;
extern volatile DWORD kb_dropped;
int resman_keyboard_init(void);
void kb_irq(PISR_FRAME frame);
int resman_keyboard_set_owner(DWORD id);
void resman_keyboard_task_gone(DWORD id);
void resman_keyboard_wait_monitor(void);
#endif
