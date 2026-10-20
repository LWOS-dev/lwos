#ifndef EXTERNAL_MEMCHECK_H
#define EXTERNAL_MEMCHECK_H
#include "abi.h"
#include "resman.h"
#include "task.h"

PVOID *lw_abi_base;
PVOID *rm_base;

static int memcheck(const LW_ENV *env, int loop) {
    if (!env || env->version!=LW_ENV_VERSION ||
        env->size<sizeof(*env) || !env->rm)return -1;
    rm_base=env->rm;
    if (!rm_base[LW_RM_SLOT_ALLOC] || !rm_base[LW_RM_SLOT_FREE] ||
        !rm_base[LW_RM_SLOT_TASK_CURRENT])return -2;
    PTASK self=*(PTASK *)rm_base[LW_RM_SLOT_TASK_CURRENT];
    if (rm_alloc(0,0,0) || rm_alloc(0xffffffffu,0,0) ||
        rm_alloc(1,3,0) || rm_alloc(1,8192,0) ||
        rm_alloc(0x7ffff000u,0,0))return -3;
    if (rm_free(0)!=RM_OK || rm_free(self)!=RM_ERR_ARGUMENT)return -4;
    BYTE *held=rm_alloc(5000,4096,0x48454c44);
    if (!held || ((DWORD)held & 4095))return -5;
    for (DWORD i=0;i<5000;i++)held[i]=(BYTE)(i+self->id);
    if (rm_free(held+1)!=RM_ERR_ARGUMENT)return -6;

    do {
        BYTE *temp=rm_alloc(37,16,0x54454d50);
        if (!temp || ((DWORD)temp & 15))return -7;
        for (DWORD i=0;i<37;i++)temp[i]=(BYTE)(i^self->id);
        /* Give preemption time to overlap two independently owned buffers. */
        for (volatile DWORD spin=0;spin<65536;spin++)asm volatile("" ::: "memory");
        for (DWORD i=0;i<37;i++)if (temp[i]!=(BYTE)(i^self->id))return -8;
        for (DWORD i=0;i<5000;i++)if (held[i]!=(BYTE)(i+self->id))return -9;
        if (rm_free(temp)!=RM_OK || rm_free(temp)!=RM_ERR_ARGUMENT)return -10;
        self->work_count++;
    } while (loop);

    /* An allocator called inside an existing critical section must leave
     * IF cleared, including when its own nested kmalloc/kfree returns. */
    DWORD before,after;
    asm volatile("pushfl; popl %0; cli" : "=r"(before) :: "memory");
    PVOID nested=rm_alloc(64,0,0);
    int rc=rm_free(nested);
    asm volatile("pushfl; popl %0" : "=r"(after) :: "memory");
    asm volatile("pushl %0; popfl" :: "r"(before) : "memory","cc");
    if (!nested || rc || (after & 0x200))return -11;
    /* F7 deliberately leaks held: task reap must collect it. F4 uses task 0,
     * which lives forever, so foreground callers free their own allocations. */
    if (!self->id && rm_free(held)!=RM_OK)return -12;
    return 29;
}
#endif
