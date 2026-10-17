#include "../include/resman.h"
#include "abi.h"
#include "mem.h"
#include "task.h"
#include "../include/tui.h"
#include "../include/isr.h"
#include "../include/keyboard.h"
#include "convert.h"

PVOID *lw_abi_base;

int resman_main(PVOID *abi)
{
    if (!abi || abi[LW_SLOT_MAGIC] != (PVOID)LW_ABI_MAGIC)
        return RM_ERR_ABI;

    lw_abi_base = abi;
    lw_puts("RESMAN STARTED AT 00180000H\n\r");

    /* Preserve resources and descriptor state when monitor calls us again. */
    static int initialized;
    if (!initialized) {
        mmap_init();
        mem_pool_init();
        gdt_init();

        if (bootstrap_tss_init())return -1;
        task_register_load();

        idt_init(resman_isr_table);
        pic_init();
        if (resman_keyboard_init())return RM_ERR_NOT_READY;
        initialized = 1;
    }

    lw_setcur(0);
    screen_fill(TUI_BLUE);

    putxy("IRQ KEYBOARD READY",0,0);
    asm volatile("sti" : : : "memory");
    
    return RM_OK;
}

DWORD resman_version(void)
{
    return 1;
}
