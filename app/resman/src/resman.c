#include "../include/resman.h"
#include "abi.h"
#include "mem.h"
#include "task.h"

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

        double_fault_init();
        task_regsiter_load();

        idt_init();
        initialized = 1;
    }

    // unfinished

    /* IDT retains monitor handlers; tasks and timer are not started yet. */
    return RM_OK;
}

DWORD resman_version(void)
{
    return 1;
}
