#include "../include/resman.h"

/* Separate from the fixed-address MEM_POOL table: monitor and resman share
 * its existing 8-byte layout. Only public rm_alloc blocks are recorded here;
 * image/stack allocations retain their existing ownership and cleanup. */
#define RM_ALLOC_MAX 16384
typedef struct {
    PVOID base;
    DWORD owner, tag;
} RM_ALLOCATION;
static RM_ALLOCATION allocations[RM_ALLOC_MAX];

PVOID resman_alloc(DWORD size, DWORD align, DWORD tag) {
    if (!size || size>0x7ffff000u ||
        (align && ((align & (align-1)) || align>4096)))return 0;
    DWORD flags=mem_irq_save();
    PVOID base=0;
    if (!current_task || current_task->state!=TASK_RUNNING)goto done;
    for (DWORD i=0; i<RM_ALLOC_MAX; i++) {
        if (allocations[i].base)continue;
        base=kmalloc((int)size);
        if (!base || base==(PVOID)-1 || base==(PVOID)-2) {
            base=0;
            break;
        }
        allocations[i]=(RM_ALLOCATION){base,current_task->id,tag};
        break;
    }
done:
    mem_irq_restore(flags);
    return base;
}

int resman_free(PVOID base) {
    if (!base)return RM_OK;
    DWORD flags=mem_irq_save();
    int rc=RM_ERR_ARGUMENT;
    if (!current_task)goto done;
    for (DWORD i=0; i<RM_ALLOC_MAX; i++) {
        RM_ALLOCATION *record=&allocations[i];
        if (record->base!=base)continue;
        if (record->owner!=current_task->id)break;
        kfree(base);
        memzero(record,sizeof(*record));
        rc=RM_OK;
        break;
    }
done:
    mem_irq_restore(flags);
    return rc;
}

void resman_alloc_reap(DWORD owner) {
    DWORD flags=mem_irq_save();
    for (DWORD i=0; i<RM_ALLOC_MAX; i++) {
        RM_ALLOCATION *record=&allocations[i];
        if (!record->base || record->owner!=owner)continue;
        kfree(record->base);
        memzero(record,sizeof(*record));
    }
    mem_irq_restore(flags);
}
