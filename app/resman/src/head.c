#include "../include/resman.h"
#include "../include/task.h"
#include "../include/keyboard.h"
#include "../include/pit.h"
#include "../include/net.h"

extern char __bss_start[], __bss_end[];

/* Fixed-address image; monitor clears BSS and calls ENTRY on its own stack.
 * Unimplemented service slots stay null. STACK_TOP is zero for this version.
 */
const PVOID lw_rm[LW_RM_SLOT_COUNT]
    __attribute__((section(".rmtab"), used, aligned(16))) = {
    [LW_RM_SLOT_NET_OPEN] = (PVOID)resman_net_open,
    [LW_RM_SLOT_NET_RECV] = (PVOID)resman_net_recv,
    [LW_RM_SLOT_NET_SEND] = (PVOID)resman_net_send,
    [LW_RM_SLOT_NET_CLOSE] = (PVOID)resman_net_close,
    [LW_RM_SLOT_NET_INFO] = (PVOID)resman_net_info,
    [LW_RM_SLOT_IMAGE_SET_CAPTION] = (PVOID)resman_image_set_caption,
    [LW_RM_SLOT_WINDOW_SET_CAPTION] = (PVOID)resman_window_set_caption,
    [LW_RM_SLOT_TEXT_SNAPSHOT_CAPTION] = (PVOID)resman_text_snapshot_caption,
    [LW_RM_SLOT_SLEEP] = (PVOID)resman_sleep,
    [LW_RM_SLOT_UPTIME_MS] = (PVOID)resman_uptime_ms,
    [LW_RM_SLOT_WINDOW_SEND] = (PVOID)resman_window_send,
    [LW_RM_SLOT_WINDOW_POLL] = (PVOID)resman_window_poll,
    [LW_RM_SLOT_WINDOW_WAIT] = (PVOID)resman_window_wait,
    [LW_RM_SLOT_TEXT_HOST] = (PVOID)resman_text_host,
    [LW_RM_SLOT_TEXT_SNAPSHOT] = (PVOID)resman_text_snapshot,
    [LW_RM_SLOT_ENABLE_INPUT] = (PVOID)resman_enable_input,
    [LW_RM_SLOT_INPUT_WAIT] = (PVOID)resman_input_wait,
    [LW_RM_SLOT_GETMPOLL] = (PVOID)resman_getmpoll,
    [LW_RM_SLOT_GETMPOS] = (PVOID)resman_getmpos,
    [LW_RM_SLOT_MAGIC] = (PVOID)LW_RM_MAGIC,
    [LW_RM_SLOT_ENTRY] = (PVOID)resman_main,
    [LW_RM_SLOT_BSS_START] = (PVOID)__bss_start,
    [LW_RM_SLOT_BSS_END] = (PVOID)__bss_end,
    [LW_RM_SLOT_IDENT] = (PVOID)LW_RM_IDENT,
    [LW_RM_SLOT_ALLOC] = (PVOID)resman_alloc,
    [LW_RM_SLOT_FREE] = (PVOID)resman_free,
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
    [LW_RM_SLOT_TASK_CREATE] = (PVOID)resman_task_create,
    [LW_RM_SLOT_TASK_EXIT] = (PVOID)resman_task_exit,
    [LW_RM_SLOT_TASK_KILL] = (PVOID)resman_task_kill,
    [LW_RM_SLOT_TASK_REAP] = (PVOID)resman_task_reap
};
