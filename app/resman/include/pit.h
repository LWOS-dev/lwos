#ifndef _LW_RM_PIT
#define _LW_RM_PIT

#include "isr.h"
#include "stdint.h"

#define PIT_CLOCK 1193182u

#define PIT_CH0 0
#define PIT_CH2 2

int pit_set_frequency(BYTE channel, DWORD hz);
int resman_pit_init(void);
void timer_irq(PISR_FRAME frame);

void pcspk_enable(void);
void pcspk_disable(void);
int pcspk_play(DWORD hz);

#endif