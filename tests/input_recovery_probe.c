/* Test-only guest: mask IRQ1 to reproduce pending input without notification. */
#include "abi.h"
#include "resman.h"
PVOID *lw_abi_base, *rm_base;
volatile DWORD stage, error;
static BYTE data[64], status[64];
static unsigned next, count, eois;
static BYTE fake_in(WORD port)
{
    if (port==0x64)return next<count ? status[next]|1 : 0;
    return data[next++];
}
static void fake_out(WORD port, BYTE value)
{
    if (port==0x20 && value==0x20)eois++;
}
static int drain_test(void)
{
    /* Simulate AUX ahead of keys, a corrupt byte and several key bytes
     * already buffered at a single IRQ edge. No GUI/mouse is active here. */
    data[0]=0x30; status[0]=0x20;
    data[1]=0x1e; status[1]=0xc0;
    data[2]=0x1e; data[3]=0x30; data[4]=0x2e;
    count=5;
    asm volatile("cli":::"memory");
    PVOID input=lw_abi_base[LW_SLOT_IO_INB], output=lw_abi_base[LW_SLOT_IO_OUTB];
    lw_abi_base[LW_SLOT_IO_INB]=(PVOID)fake_in;
    lw_abi_base[LW_SLOT_IO_OUTB]=(PVOID)fake_out;
    ((void (*)(PVOID))KEYBOARD_IRQ_ADDRESS)(0);
    int ok=next==5 && eois==1;
    lw_abi_base[LW_SLOT_IO_INB]=input; lw_abi_base[LW_SLOT_IO_OUTB]=output;
    asm volatile("sti":::"memory");
    ok=ok && rm_getmpoll()=='a' && rm_getmpoll()=='b' &&
        rm_getmpoll()=='c' && rm_getmpoll()==VK_NONE;
    next=0; count=40;
    for (unsigned i=0;i<count;i++) {data[i]=0x1e;status[i]=0;}
    asm volatile("cli":::"memory");
    lw_abi_base[LW_SLOT_IO_INB]=(PVOID)fake_in;
    lw_abi_base[LW_SLOT_IO_OUTB]=(PVOID)fake_out;
    ((void (*)(void))KEYBOARD_POLL_ADDRESS)();
    ok=ok && next==32; /* finite IRQ work, remaining bytes on next pass */
    ((void (*)(void))KEYBOARD_POLL_ADDRESS)();
    ok=ok && next==40;
    lw_abi_base[LW_SLOT_IO_INB]=input; lw_abi_base[LW_SLOT_IO_OUTB]=output;
    asm volatile("sti":::"memory");
    for (unsigned i=0;i<40;i++)ok=ok && rm_getmpoll()=='a';
    return ok && rm_getmpoll()==VK_NONE;
}
static volatile DWORD *const ticks=(volatile DWORD *)TIMER_TICKS_ADDRESS;
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env)
{
    lw_abi_base=env->abi; rm_base=env->rm;
    /* Software INT leaves PIC ISR clear, like a spurious hardware vector.
     * Previously either vector took the unhandled-interrupt halt path. */
    asm volatile("int $0x27; int $0x2f":::"memory");
    if (!drain_test()) {error=10;stage=4;return -1;}
    BYTE mask=lw_inb(0x21);
    for (DWORD phase=1; phase<=3; phase++) {
        asm volatile("cli":::"memory");
        lw_outb(0x21,phase==2 ? mask : mask|2);
        if (phase==3)*ticks=0xfffffff0u;
        stage=phase;
        asm volatile("sti":::"memory");
        int key;
        for (;;) {
            key=rm_getmpoll();
            if (key!=VK_NONE && !(key&VK_KEYUP))break;
            rm_input_wait();
        }
        if (key!=(int)('a'+phase-1)) {error=phase;break;}
        /* Let the release arrive before the next phase. */
        rm_sleep(150);
        while (rm_getmpoll()!=VK_NONE) {}
    }
    lw_outb(0x21,mask);
    stage=4;
    return error ? -1 : 42;
}
