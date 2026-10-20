#include "abi.h"
#include "resman.h"
#include "task.h"

PVOID *lw_abi_base;

__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version != LW_ENV_VERSION ||
        env->size < sizeof(*env) || !env->abi)
        return -1;
    lw_abi_base = env->abi;
    if (!LW_ABI_VALID() || !lw_abi_base[LW_SLOT_TRAMP_CALL] ||
        !lw_abi_base[LW_SLOT_CONSOLE_GETK] ||
        !lw_abi_base[LW_SLOT_IO_VIDEO_EXIT])
        return -2;

    if (!env->rm || !env->rm[LW_RM_SLOT_TASK_CURRENT])return -3;
    PTASK self=*(PTASK *)env->rm[LW_RM_SLOT_TASK_CURRENT];
    /* Never yield: advancing both counters demonstrates timer preemption. */
    for (;;)self->work_count+=1;
    return 0;
}
