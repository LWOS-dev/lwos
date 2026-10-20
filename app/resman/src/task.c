#include "../include/task.h"
#include "../include/resman.h"

#include "abi.h"

PSEG_DESC sys_gdt;
GDTR sys_gdtr;
PINT_DESC sys_idt;
IDTR sys_idtr;
static TASK_RUNTIME runtime[TASK_MAX];

void gdt_update(void) {
    asm volatile("lgdt %0" : : "m"(sys_gdtr) : "memory");
}

int find_free_gdt(void) {
    /* 前五项是固定段；本函数只查找，不预留或修改槽位。 */
    if (!sys_gdt) return -1;
    for (int i = 5; i < SEG_MAX; i++) {
        if (!(sys_gdt[i].access & 0x80)) return i;
    }
    return -1;
}

void gdt_init(void) {
    sys_gdt = (PSEG_DESC)0x360000;
    sys_gdtr.base = (DWORD)sys_gdt;
    sys_gdtr.limit = SEG_MAX * sizeof(SEG_DESC) - 1;
    memzero(sys_gdt, SEG_MAX * sizeof(SEG_DESC));

    seg_write(sys_gdt + 1, 0, 0xfffff, GDT_ACC_CODE_RX);
    seg_write(sys_gdt + 2, 0, 0xfffff, GDT_ACC_DATA_RW);

    /* BIOS 跳板的 16 位段：字节粒度，D/B=0，与 stage2 一致。 */
    SMP_SEG segment = {
        .base = 0, .limit = 0xffff, .flag = GDT_ACC_CODE_RX, .attr = 0,
    };
    seg_xlat_in(&segment, sys_gdt + 3);
    segment.flag = GDT_ACC_DATA_RW;
    seg_xlat_in(&segment, sys_gdt + 4);
    gdt_update();
}

void idt_update(void) {
    asm volatile("lidt %0" : : "m"(sys_idtr) : "memory");
}

void idt_init(PCDWORD entries) {
    /* 接管期间禁止普通 IRQ，整张表构造完成后才执行 lidt。 */
    DWORD flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    IDTR previous;
    asm volatile("sidt %0" : "=m"(previous) : : "memory");
    sys_idt = (PINT_DESC)0x370000;
    if (previous.base != (DWORD)sys_idt) {
        DWORD bytes = (DWORD)previous.limit + 1;
        DWORD capacity = INT_MAX * sizeof(INT_DESC);
        if (bytes > capacity) bytes = capacity;
        memzero(sys_idt, capacity);
        memcpy(sys_idt, (PCVOID)previous.base, bytes);
    }
    for (DWORD i = 0; i < INT_MAX; i++) {
        /* 保留单步/断点的 monitor 调试路径；暂不更换 #DF 入口。 */
        if (i == 8 || ((i == 1 || i == 3) && (sys_idt[i].attr & 8)))
            continue;
        SMP_INT gate = {
            .sel = 0x08,
            .type = INT_INT32,
            .attr = entries[i] ? INT_RING_0 : 0,
            .offset = entries[i],
        };
        int_xlat_write(sys_idt + i, &gate);
    }
    sys_idtr.base = (DWORD)sys_idt;
    sys_idtr.limit = INT_MAX * sizeof(INT_DESC) - 1;
    idt_update();
    asm volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static TSS32 df_tss;
static PVOID df_stack;

extern void double_fault_entry(void);

void double_fault_init() {
    df_stack=kmalloc(8192);
    if (!df_stack || df_stack==(PVOID)-1 || df_stack==(PVOID)-2)
        return;
    memzero(&df_tss, sizeof(df_tss));

    df_tss.eip=(DWORD)double_fault_entry;
    df_tss.esp=(DWORD)df_stack+8192;
    df_tss.cs=0x08;
    df_tss.ss=0x10;
    df_tss.ds=0x10;
    df_tss.es=0x10;
    df_tss.fs=0x10;
    df_tss.gs=0x10;
    df_tss.eflags=0x02;
    df_tss.ss0=0x10;
    df_tss.esp0=df_tss.esp;
    df_tss.cr3=0;
    df_tss.ldt=0;
    df_tss.iomap_base=sizeof(TSS32);

    int df_index=find_free_gdt();
    if (df_index<0)return;
    SMP_SEG desc={
        .base=(DWORD)&df_tss,
        .limit=sizeof(TSS32)-1,
        .flag=GDT_ACC_TSS32_AVAIL,
        .attr=0,
    };
    seg_xlat_in(&desc,sys_gdt+df_index);

    SMP_INT gate={
        .sel=df_index*8,
        .type=INT_TASK_GATE,
        .attr=INT_RING_0,
        .offset=0,
    };
    int_xlat_write(sys_idt+8,&gate);
}

TASK task[TASK_MAX];
PTASK current_task;

int bootstrap_tss_init() {
    int index=find_free_gdt();
    if (index<0)return -1;
    memzero(&task[0], sizeof(task[0]));
    task[0].id=0;
    memcpy(task[0].name,"MONITOR",8);
    task[0].state=TASK_RUNNING;
    task[0].tss_selector=index*8;
    task[0].tss.iomap_base=sizeof(TSS32);
    task[0].tss.ss0=0x10;
    SMP_SEG desc={
        .base=(DWORD)&task[0].tss,
        .limit=sizeof(TSS32)-1,
        .flag=GDT_ACC_TSS32_AVAIL,
        .attr=0,
    };
    seg_xlat_in(&desc,sys_gdt+index);

    current_task=&task[0];

    return 0;
}
void task_register_load(void) {
    WORD sel=current_task->tss_selector;
    asm volatile ("ltr %0" : : "rm"(sel) : "memory");
}
int resman_task_acquire(PTASK *out) {
    if (!out)return RM_ERR_ARGUMENT;
    *out=0;
    for (int i=0; i<TASK_MAX; i++) {
        if (task[i].state==TASK_FREE) {
            PTASK p=&task[i];
            memzero(p,sizeof(TASK));
            p->id=i;
            p->state=TASK_CREATING;
            *out=p;
            return RM_OK;
        }
    }
    return RM_ERR_NOT_READY;
}

extern void task_start(void);

int resman_task_create(PRM_IMAGE image, PCLW_ENV env, PDWORD out_id) {
    if (!out_id)return RM_ERR_ARGUMENT;

    *out_id=0;
    int rc=RM_ERR_ARGUMENT;
    DWORD ef;
    asm volatile("pushfl; popl %0; cli":"=r"(ef)::"memory");

    PTASK tk;

    if (!current_task || !sys_gdt) {
        rc=RM_ERR_NOT_READY;
        goto done;
    }
    if (!resman_image_valid(image))goto done;
    if (image->state!=RM_IMAGE_LOADED)goto done;
    if (!image->base || !image->stack || !image->entry)goto done;
    if (env) {
        if (env->version!=LW_ENV_VERSION ||
            env->size < sizeof(LW_ENV)) {
            goto done;
        }
        if (env->argc!=0 || env->argv!=0) {
            rc=RM_ERR_NOT_IMPLEMENTED;
            goto done;
        }
    }

    DWORD stack_base=(DWORD)image->stack;
    if (image->stack_size>0xffffffffu-stack_base)goto done;
    DWORD stack_top=(stack_base+image->stack_size)&0xfffffff0u;
    if (stack_top<=stack_base||stack_top-stack_base<0x2000u)goto done;
    int err=resman_task_acquire(&tk);
    if (err) {rc=err;goto done;}
    int index=find_free_gdt();
    if (index<0) {rc=RM_ERR_NOT_READY;goto rollback;}
    DWORD id=tk->id;
    PTASK_RUNTIME rt=&runtime[id];

    memzero(rt,sizeof(TASK_RUNTIME));
    rt->image=image;

    if (env) {
        rt->env = *env;
    } else {
        rt->env.version = LW_ENV_VERSION;
        rt->env.size = sizeof(LW_ENV);
        rt->env.abi = lw_abi_base;
        rt->env.rm = (PVOID *)lw_rm;
    }

    tk->entry=image->entry;
    tk->image_base=(DWORD)image->base;
    tk->image_size=image->mem_size;
    tk->stack_base=(DWORD)image->stack;
    tk->stack_size=image->stack_size;
    tk->tss_selector=(WORD)(index<<3);

    PTSS32 tss = &tk->tss;
    tss->eip=(DWORD)task_start;
    tss->esp=stack_top;
    tss->eflags=0x202;
    tss->cs = 0x08;
    tss->ss = 0x10;
    tss->ds = 0x10;
    tss->es = 0x10;
    tss->fs = 0x10;
    tss->gs = 0x10;
    tss->ss0 = 0x10;
    tss->esp0 = stack_top;
    tss->iomap_base = sizeof(TSS32);
    SMP_SEG desc = {
        .base = (DWORD)tss,
        .limit = sizeof(TSS32) - 1,
        .flag = GDT_ACC_TSS32_AVAIL,
        .attr = 0,
    };
    seg_xlat_in(&desc, sys_gdt + index);
    image->state=RM_IMAGE_BOUND;
    *out_id=id;
    asm volatile("":::"memory");
    tk->state = TASK_READY;
    rc=RM_OK;
    goto done;
rollback:
    memzero(&runtime[tk->id], sizeof(runtime[0]));
    memzero(tk, sizeof(*tk));
done:
    asm volatile("pushl %0; popfl"::"r"(ef):"memory","cc");
    return rc;
}

/* Called with IRQs disabled. JMP switches TSS without nesting task backlinks.
 * A resumed task returns here, then unwinds its own timer interrupt frame. */
void task_schedule(void) {
    if (!current_task)return;
    PTASK previous=current_task;
    for (DWORD offset=1; offset<TASK_MAX; offset++) {
        PTASK next=&task[(previous->id+offset)%TASK_MAX];
        if (next->state!=TASK_READY)continue;
        if (previous->state==TASK_RUNNING)previous->state=TASK_READY;
        next->state=TASK_RUNNING;
        current_task=next;
        struct { DWORD offset; WORD selector; } __attribute__((packed)) target={0,next->tss_selector};
        asm volatile("ljmp *%0" :: "m"(target) : "memory");
        return;
    }
}

void task_bootstrap(void) {
    PTASK self=current_task;
    PTASK_RUNTIME rt=&runtime[self->id];
    int result=((LWP_ENTRY)self->entry)(&rt->env);
    asm volatile("cli" ::: "memory");
    self->exit_code=result;
    self->state=TASK_DEAD;
    /* Keep the image/stack bound: this suspended context still owns them. */
    task_schedule();
    for (;;)asm volatile("hlt");
}
