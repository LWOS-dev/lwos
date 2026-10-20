#ifndef _RESMAN_TASK_H
#define _RESMAN_TASK_H

#include "../../../lib/include/task.h"
#include "../../../lib/include/resman.h"

/* RESMAN 私有运行时状态；应用通过映像头槽位访问。 */
typedef struct _TASK_RUNTIME {
    PRM_IMAGE image;
    LW_ENV env;
} TASK_RUNTIME, *PTASK_RUNTIME;

extern PSEG_DESC sys_gdt;
extern GDTR sys_gdtr;
extern PINT_DESC sys_idt;
extern IDTR sys_idtr;
extern TASK task[TASK_MAX];
extern PTASK current_task;

void task_schedule(void);
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

#endif
