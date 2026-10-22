#include "stdint.h"
#include "abi.h"
#include "../include/resman.h"
#include "../include/pit.h"
#include "../include/task.h"

static int pit_set_divisor(BYTE channel, DWORD divisor) {
    if ((channel!=PIT_CH0 && channel!=PIT_CH2) || divisor<2 || divisor>65536)
        return -1;
    lw_outb(0x43,channel==PIT_CH0 ? 0x34 : 0xb6);
    lw_outb(0x40+channel,(BYTE)divisor);
    lw_outb(0x40+channel,(BYTE)(divisor>>8));
    return 0;
}
int pit_set_frequency(BYTE channel, DWORD hz) {
    if (!hz || hz>PIT_CLOCK)return -1;
    return pit_set_divisor(channel,(PIT_CLOCK+hz/2)/hz);
}

void pcspk_enable(void) {
    BYTE v=lw_inb(0x61);
    lw_outb(0x61,v|0x03);
}
void pcspk_disable(void) {
    BYTE v=lw_inb(0x61);
    lw_outb(0x61,v&0xfc);
}
int pcspk_play(DWORD hz) {
    if (pit_set_frequency(PIT_CH2,hz)!=0)return -1;
    pcspk_enable();
    return 0;
}

volatile DWORD timer_ticks;
static volatile DWORD uptime_ms;
static DWORD ms_fraction, quantum_ticks;

DWORD resman_uptime_ms(void) {
    return uptime_ms;
}
void timer_irq(PISR_FRAME frame) {
    (void)frame;
    timer_ticks++;
    /* 1194/1193182 s = 1.00068556 ms. Accumulate the fractional ms without
     * a 64-bit division/libgcc dependency in the kernel. Counter wraps at 2^32.
     */
    uptime_ms++;
    ms_fraction+=PIT_TIMER_DIVISOR*1000u-PIT_CLOCK;
    if (ms_fraction>=PIT_CLOCK) {ms_fraction-=PIT_CLOCK;uptime_ms++;}
    if (current_task)current_task->run_ticks++;
    int woke=task_timer_tick(timer_ticks);
    int preempt=++quantum_ticks>=10;
    if (preempt)quantum_ticks=0;
    /* EOI before switching, so the next task can receive IRQ0. Wake sleepers
     * at the 1 ms timer rate, while ordinary preemption stays around 10 ms.
     */
    lw_outb(0x20,0x20);
    if (woke || preempt)task_schedule();
}
static DWORD lock(void) {
    DWORD flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void unlock(DWORD flags) {
    asm volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

// 初始化定时器 IRQ0
int resman_pit_init(void) {
    DWORD flags=lock();
    /* A ceil-rounded divisor gives a minimum tick period of 1 ms. */
    if (pit_set_divisor(PIT_CH0,PIT_TIMER_DIVISOR)!=0) {
        unlock(flags);
        return -1;
    }
    if (resman_isr_set_handler(0x20,timer_irq)) {
        unlock(flags);
        return -1;
    }
    pic_irq_set(0,1);
    unlock(flags);
    return 0;
}

void resman_alarm() {
    pcspk_disable();
    pcspk_play(440);
    lw_getk();
    pcspk_disable();
}

void resman_pcspk(int hz) {
    pcspk_play(hz);
}
void resman_pcspk_shut() {
    pcspk_disable();
}