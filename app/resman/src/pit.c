#include "stdint.h"
#include "abi.h"
#include "../include/resman.h"
#include "../include/pit.h"

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

// 初始化定时器 IRQ0
int resman_pit_init(void) {
    ;
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

void resman_alarm() {
    pcspk_disable();
    pcspk_play(440);
    lw_getk();
    pcspk_disable();
}