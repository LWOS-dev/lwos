#include "stdint.h"
#include "abi.h"
#include "../include/resman.h"
#include "../include/pit.h"
#include "../include/task.h"

int pit_set_frequency(BYTE channel, DWORD hz) {
    DWORD divisor;
    WORD port;
    BYTE command;

    if (channel != PIT_CH0 && channel != PIT_CH2)return -1;
    if (hz == 0)return -1;

    divisor=(PIT_CLOCK+hz/2)/hz;

    if (divisor<2 || divisor>65536)return -1;

    port=0x40+channel;

    if (channel == PIT_CH0)
        command = 0x34; // Mode 2
    else
        command = 0xB6; // Mode 3

    lw_outb(0x43, command);
    lw_outb(port, (BYTE)divisor);
    lw_outb(port, (BYTE)(divisor >> 8));

    return 0;
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

#define PIT_TICK_HZ 100u

void timer_irq(PISR_FRAME frame) {
    (void)frame;
    
    if (current_task)current_task->run_ticks++;
    /* EOI before switching, so the next task can receive IRQ0. */
    lw_outb(0x20,0x20);
    task_schedule();
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
    /* 16 位分频器不能直接产生 1 Hz，使用软件计数分频。 */
    if (pit_set_frequency(PIT_CH0,PIT_TICK_HZ)!=0) {
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