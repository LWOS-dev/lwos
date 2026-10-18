#include "../include/keyboard.h"
#include "abi.h"

#define QUEUE_SIZE 128u
#define WAIT_LIMIT 100000u

static BYTE scans[QUEUE_SIZE];
static volatile DWORD head, tail;
volatile DWORD kb_irq_cnt;
volatile BYTE kb_last_sc;
volatile DWORD kb_dropped;

static DWORD lock(void)
{
    DWORD flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void unlock(DWORD flags)
{
    asm volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

/* IRQ 和前台命令事务都在 IF=0 时入队。满时丢弃新字节并计数。 */
static void enqueue(BYTE scan)
{
    DWORD next=(head+1)&(QUEUE_SIZE-1);
    if (next==tail) {
        kb_dropped++;
        return;
    }
    scans[head]=scan;
    head=next;
}

void kb_irq(PISR_FRAME frame)
{
    (void)frame;
    BYTE status=lw_inb(0x64);
    if ((status&0x21)==0x01) {
        BYTE scan=lw_inb(0x60);
        if (!(status&0xc0)) {
            kb_last_sc=scan;
            kb_irq_cnt++;
            enqueue(scan);
        }
    }
    lw_outb(0x20,0x20);
}

static int read_scan(PBYTE scan)
{
    DWORD flags=lock();
    int available=head!=tail;
    if (available) {
        *scan=scans[tail];
        tail=(tail+1)&(QUEUE_SIZE-1);
    }
    unlock(flags);
    return available;
}

static void wait_scan(void)
{
    DWORD flags=lock();
    /* 不擅自开启调用者关闭的中断；阻塞输入应在 IF=1 的前台使用。 */
    if (head==tail && (flags&0x200))
        asm volatile("sti; hlt; cli" : : : "memory");
    unlock(flags);
}

/* 在前台短暂关闭中断，防止 IRQ 抢走命令 ACK。
 * 等回复期间的扫描码仍入队，不当作 ACK 丢弃。
 */
static int send_acknowledged(BYTE byte)
{
    for (int retry=0; retry<3; retry++) {
        DWORD tries;
        for (tries=0; tries<WAIT_LIMIT; tries++)
            if (!(lw_inb(0x64)&2))break;
        if (tries==WAIT_LIMIT)return 0;
        lw_outb(0x60,byte);
        for (tries=0; tries<WAIT_LIMIT; tries++) {
            BYTE status=lw_inb(0x64);
            if ((status&0x21)!=1)continue;
            BYTE reply=lw_inb(0x60);
            if (status&0xc0)continue;
            if (reply==0xfa)return 1;
            if (reply==0xfe)break;
            enqueue(reply);
        }
        if (tries==WAIT_LIMIT)return 0;
    }
    return 0;
}

static void set_leds(BYTE value)
{
    DWORD flags=lock();
    if (send_acknowledged(0xed))send_acknowledged(value);
    unlock(flags);
}

int resman_keyboard_init(void) // 初始化键盘 IRQ1
{
    static const LW_KBD_SOURCE source={read_scan,wait_scan,set_leds};
    DWORD flags=lock();
    if (!lw_abi_base[LW_SLOT_IO_KBD_BIND_SOURCE] || !lw_kbd_enable()) {
        unlock(flags);
        return -1;
    }
    head=tail=0;
    kb_irq_cnt=kb_dropped=0;
    kb_last_sc=0;
    if (resman_isr_set_handler(0x21,kb_irq) || lw_kbd_bind_source(&source)) {
        unlock(flags);
        return -1;
    }
    pic_irq_set(1,1);
    unlock(flags);
    return 0;
}
