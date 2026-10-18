#include "../include/task.h"

PSEG_DESC sys_gdt;
GDTR sys_gdtr;
PINT_DESC sys_idt;
IDTR sys_idtr;

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
