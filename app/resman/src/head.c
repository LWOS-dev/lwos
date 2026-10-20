#include "../include/resman.h"
#include "../include/task.h"

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
    [LW_RM_SLOT_IMAGE_ACQUIRE] = (PVOID)resman_image_acquire,
    [LW_RM_SLOT_IMAGE_LOAD] = (PVOID)resman_image_load,
    [LW_RM_SLOT_IMAGE_RUN] = (PVOID)resman_image_run,
    [LW_RM_SLOT_IMAGE_UNLOAD] = (PVOID)resman_image_unload,
    [LW_RM_SLOT_ALARM] = (PVOID)resman_alarm,
    [LW_RM_SLOT_ALARM_FREQ] = (PVOID)resman_pcspk,
    [LW_RM_SLOT_ALARM_SHUT] = (PVOID)resman_pcspk_shut,
    [LW_RM_SLOT_TASK_TABLE] = (PVOID)task,
    [LW_RM_SLOT_TASK_CURRENT] = (PVOID)&current_task,
    [LW_RM_SLOT_TASK_ACQUIRE] = (PVOID)resman_task_acquire,
    [LW_RM_SLOT_TASK_CREATE] = (PVOID)resman_task_create
};
