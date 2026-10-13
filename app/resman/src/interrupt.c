#include "../include/isr.h"
#include "../include/tui.h"


static BYTE inb(WORD port) {
    BYTE val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static void outb(WORD port, BYTE val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static WORD inw(WORD port) {
    WORD val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static void outw(WORD port, WORD val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static DWORD inl(WORD port) {
    DWORD val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static void outl(WORD port, DWORD val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static ISR_HANDLER handlers[256];

int resman_isr_set_handler(DWORD vector, ISR_HANDLER handler)
{
    /* 这三项仍由继承的 monitor 入口负责，不经过本分发器。 */
    if (vector >= 256 || vector == 1 || vector == 3 || vector == 8)
        return -1;

    DWORD flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    handlers[vector] = handler;
    asm volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
    return 0;
}

static void print_hex(DWORD value, int x, int y)
{
    static const char digits[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++)
        putxyc(digits[(value >> ((7 - i) * 4)) & 0xf], x + i, y);
}

void resman_isr_dispatch(PISR_FRAME frame)
{
    if (frame->vector < 256 && handlers[frame->vector]) {
        handlers[frame->vector](frame);
        return;
    }

    /* IRQ 尚未启用；没有注册处理函数的向量先报错停机，不静默吞掉。 */
    set_attr(TUI_WHITE | TUI_LIGHT, TUI_RED);
    putxy("RESMAN UNHANDLED INTERRUPT", 0, 0);
    putxy("VECTOR=", 0, 1);
    print_hex(frame->vector, 7, 1);
    putxy("ERROR =", 0, 2);
    print_hex(frame->error, 7, 2);
    putxy("EIP   =", 0, 3);
    print_hex(frame->eip, 7, 3);
    for (;;) asm volatile("cli; hlt" : : : "memory");
}

static inline void io_wait(void) {
	outb(0x80, 0);
}

void pic_init() {
    outb(0x20,0x11); outb(0xa0,0x11);
    io_wait();
    outb(0x21,0x20); outb(0xa1,0x28);
    io_wait();
    outb(0x21,0x04); outb(0xa1,0x02);
    io_wait();
    outb(0x21,0x01); outb(0xa1,0x01);
    io_wait();
    outb(0x21,0xff); outb(0xa1,0xff);
    io_wait();
}

void pic_irq_set(DWORD irq, int enable) {
    WORD port;
    BYTE mask;
    BYTE bit;

    if (irq>=16 || irq==2) {
        return;
    }

    port=(irq<8)?0x21:0xa1;
    bit=1<<(irq&7);
    mask=inb(port);

    if (enable) {
        mask&=~bit;
    } else {
        mask|=bit;
    }

    outb(port,mask);

    if (irq>=8) {
        BYTE master=inb(0x21);
        if (mask!=0xff) {
            master&=~(1<<2);
        } else {
            master|=(1<<2);
        }
        outb(0x21,master);
    }
}

