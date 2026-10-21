#include "../include/resman.h"
#include "abi.h"
#include "mem.h"
#include "../include/task.h"
#include "../include/tui.h"
#include "../include/isr.h"
#include "../include/keyboard.h"
#include "../include/pit.h"
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
        if (task_idle_init()!=RM_OK)return RM_ERR_NOT_READY;
        if (resman_keyboard_init())return RM_ERR_NOT_READY;
        if (resman_pit_init())return RM_ERR_NOT_READY;
        initialized = 1;
        /* Failure is nonfatal: retain monitor input and its text UI. */

        if (resman_autoload_ghost()==RM_OK && resman_enable_input()!=RM_OK)
            lw_puts("MOUSE INPUT UNAVAILABLE\n\r");
    }

    lw_setcur(0);
    asm volatile("sti" : : : "memory");
    /* Remain on monitor's stack, but yield its CPU while host owns input.
     * Do not let monitor initialize/redraw its text UI over the GUI host. */
    resman_keyboard_wait_monitor();
    if (lw_gfx_is_live()) lw_gfx_exit();

    return RM_OK;
}

DWORD resman_version(void)
{
    return 1;
}
