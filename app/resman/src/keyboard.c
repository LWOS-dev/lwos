#include "../include/keyboard.h"
#include "../include/task.h"
#include "abi.h"

#define QUEUE_SIZE 128u
#define WAIT_LIMIT 100000u

static BYTE scans[QUEUE_SIZE];
static volatile DWORD head, tail;
volatile DWORD kb_irq_cnt;
volatile BYTE kb_last_sc;
volatile DWORD kb_dropped;

DWORD keyboard_owner=0;

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
    task_wake_locked(keyboard_owner,TASK_WAIT_KEYBOARD);
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
    if (!current_task || current_task->id!=keyboard_owner) {
        unlock(flags);
        return 0;
    }
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
    if (!(flags&0x200)||!current_task) {
        unlock(flags);
        return;
    }
    while (1) {
        int mine=current_task&&current_task->id==keyboard_owner;
        if (mine&&head!=tail)break;
        task_block_locked(TASK_WAIT_KEYBOARD);
    }
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

int resman_keyboard_set_owner(DWORD id) {
    DWORD flags=lock();
    int rc=RM_ERR_ARGUMENT;
    if (id>=TASK_MAX)goto done;
    DWORD stt=task[id].state;
    if (stt!=TASK_READY&&stt!=TASK_RUNNING&&stt!=TASK_WAITING)goto done;
    keyboard_owner=id;
    if (id==0 || head!=tail)task_wake_locked(id,TASK_WAIT_KEYBOARD);
    rc=RM_OK;
done:
    unlock(flags);
    return rc;
}

/* Exit/kill call this with IRQs masked, before a task ID can be reused. */
void resman_keyboard_task_gone(DWORD id)
{
    DWORD flags = lock();
    if (keyboard_owner == id) {
        keyboard_owner = 0;
        /* Drop keys intended for the old UI. Wake monitor even without data:
         * its boot wait is for ownership, not for a key to consume. */
        tail = head;
        task_wake_locked(0, TASK_WAIT_KEYBOARD);
    }
    unlock(flags);
}

void resman_keyboard_wait_monitor(void)
{
    DWORD flags = lock();
    if ((flags & 0x200) && current_task == &task[0]) {
        while (keyboard_owner != 0)
            task_block_locked(TASK_WAIT_KEYBOARD);
    }
    unlock(flags);
}
