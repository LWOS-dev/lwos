#include "../include/resman.h"

extern char __bss_start[], __bss_end[];

/* Fixed-address image; monitor clears BSS and calls ENTRY on its own stack.
 * Unimplemented service slots stay null. STACK_TOP is zero for this version.
 */
const PVOID lw_rm[LW_RM_SLOT_COUNT]
    __attribute__((section(".rmtab"), used, aligned(16))) = {
    [LW_RM_SLOT_MAGIC] = (PVOID)LW_RM_MAGIC,
    [LW_RM_SLOT_ENTRY] = (PVOID)resman_main,
    [LW_RM_SLOT_BSS_START] = (PVOID)__bss_start,
    [LW_RM_SLOT_BSS_END] = (PVOID)__bss_end,
    [LW_RM_SLOT_IDENT] = (PVOID)LW_RM_IDENT,
    [LW_RM_SLOT_VERSION] = (PVOID)resman_version,
};
