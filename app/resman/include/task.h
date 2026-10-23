#ifndef _RESMAN_TASK_H
#define _RESMAN_TASK_H

#include "../../../lib/include/task.h"
#include "../../../lib/include/resman.h"

typedef enum {
    TASK_WAIT_NONE,
    TASK_WAIT_KEYBOARD,
    TASK_WAIT_TIMER,
    TASK_WAIT_WINDOW,
    TASK_WAIT_NET
} TASK_WAIT_REASON; // 等待理由

/* RESMAN 私有运行时状态；应用通过映像头槽位访问。 */
typedef struct _TASK_RUNTIME {
    PRM_IMAGE image;
    LW_ENV env;
    DWORD wait_reason; // 等待理由
    BYTE fpu_state[108]; /* 32-bit FNSAVE/FRSTOR image (x87 only). */
    BYTE fpu_valid;
} TASK_RUNTIME, *PTASK_RUNTIME;

extern PSEG_DESC sys_gdt;
extern GDTR sys_gdtr;
extern PINT_DESC sys_idt;
extern IDTR sys_idtr;
extern TASK task[TASK_MAX];
extern PTASK current_task;

void task_schedule(void);
int task_timer_tick(DWORD now);
void task_bootstrap(void);
void gdt_init(void);
void gdt_update(void);
int find_free_gdt(void);
/* 保留 monitor 的 #DB/#BP 和向量 8。 */
void idt_init(PCDWORD entries);
void idt_update(void);
void double_fault_init(void);
void task_register_load(void);
int bootstrap_tss_init(void);
int task_idle_init(void);
void task_block_locked(DWORD reason);
void task_wake_locked(DWORD id, DWORD reason);

#endif
