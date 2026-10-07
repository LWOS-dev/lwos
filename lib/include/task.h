#ifndef _TASK_H
#define _TASK_H

#include "mem.h"

#define SEG_FLAGS   0x00C0000000000000

/* Present=1，DPL=0 */
#define GDT_ACC_DATA_RO       0x90  /* 数据：只读 */
#define GDT_ACC_DATA_RW       0x92  /* 数据：读写，也用于普通栈 */
#define GDT_ACC_STACK_DOWN    0x96  /* 数据：读写、向下扩展 */
#define GDT_ACC_CODE_X        0x98  /* 代码：只执行 */
#define GDT_ACC_CODE_RX       0x9A  /* 代码：执行、可读 */

/* 系统描述符，S=0 */
#define GDT_ACC_LDT           0x82  /* LDT */
#define GDT_ACC_TSS32_AVAIL   0x89  /* 可用的 32 位 TSS */
#define GDT_ACC_TSS32_BUSY    0x8B  /* 忙的 32 位 TSS，由 CPU 管理 */

#define SEG_MAX 8192

typedef QWORD SEG_DESC;
typedef PQWORD PSEG_DESC;

extern PSEG_DESC sys_gdt;

typedef struct _GDTR {
    DWORD base;
    WORD limit;
} GDTR, *PGDTR;
typedef struct _SMP_SEG {
    DWORD base;
    DWORD limit;
    DWORD flag;
    DWORD resv;
} SMP_SEG, *PSMP_SEG;

typedef struct _TSS32 {
    DWORD backlink;     // +0h

    DWORD esp0, ss0;    // +4h, +8h
    DWORD esp1, ss1;    // +ch, +10h
    DWORD esp2, ss2;    // +14h, +18h

    DWORD cr3;          // +1ch
    DWORD eip, eflags;  // +20h, +24h
    DWORD eax, ecx, edx, ebx;       // +28h, +2ch, +30h, +34h
    DWORD esp, ebp, esi, edi;       // +38h, +3ch, +40h, +44h
    DWORD es, cs, ss, ds, fs, gs;   // +48h, +4ch, +50h, +54h, +58h, +5ch
    DWORD ldt;          // +60h

    WORD trap;          // +64h
    WORD iomap_base;    // +66h
} TSS32;

_Static_assert(sizeof(TSS32) == 104, "TSS32 size");

typedef enum _TASK_STATE {
    TASK_FREE,
    TASK_CREATING,
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING,
    TASK_DEAD
} TASK_STATE;

typedef struct _TASK {
    DWORD id;           //  +0h: unique index
    DWORD state;        //  +4h: task state
    CHAR name[16];      //  +8h: task name

    DWORD entry;        // +18h: entry addr after loaded
    DWORD image_base;   // +1ch
    DWORD image_size;   // +20h: mem used include bss
    DWORD stack_base;   // +24h
    DWORD stack_size;   // +28h

    DWORD wake_tick;    // +2ch: sleep time left
    DWORD run_ticks;    // +30h: ran ticks elapsed
    int exit_code;      // +34h

    WORD tss_selector;  // +38h
    WORD reserved;      // +3ah

    TSS32 tss;          // +3ch
} TASK, *PTASK;

void gdt_init();
void gdt_update();

void seg_xlat(PSMP_SEG dest, PSEG_DESC src);
void seg_xlat_in(PSMP_SEG src, PSEG_DESC dest);
void seg_write(PSEG_DESC dest, DWORD base, DWORD limit, BYTE flags);

#endif