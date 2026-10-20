#include "abi.h"
#include "resman.h"
#include "task.h"

PVOID *lw_abi_base;
PVOID *rm_base;

/* F7: validate rejected lifecycle operations, then explicitly exit with 7.
 * F4: task 0 cannot exit, so return normally to the foreground caller. */
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version!=LW_ENV_VERSION ||
        env->size<sizeof(*env) || !env->rm)return -1;
    rm_base=env->rm;
    if (!rm_base[LW_RM_SLOT_TASK_EXIT] || !rm_base[LW_RM_SLOT_TASK_KILL] ||
        !rm_base[LW_RM_SLOT_TASK_REAP] || !rm_base[LW_RM_SLOT_TASK_CURRENT])
        return -2;
    PTASK self=*(PTASK *)rm_base[LW_RM_SLOT_TASK_CURRENT];
    int code=123;
    if (rm_task_kill(0)!=RM_ERR_ARGUMENT ||
        rm_task_reap(0,&code)!=RM_ERR_ARGUMENT ||
        rm_task_kill(TASK_MAX)!=RM_ERR_ARGUMENT ||
        rm_task_reap(TASK_MAX,&code)!=RM_ERR_ARGUMENT ||
        rm_task_reap(self->id,0)!=RM_ERR_ARGUMENT ||
        rm_task_kill(self->id)!=RM_ERR_ARGUMENT ||
        rm_task_reap(self->id,&code)!=RM_ERR_ARGUMENT ||
        code!=123)return -3;
    int rc=rm_task_exit(7);
    return self->id==0 && rc==RM_ERR_ARGUMENT ? 7 : -4;
}
