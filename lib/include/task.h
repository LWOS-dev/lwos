#ifndef _TASK_H
#define _TASK_H

#include "mem.h"

#define SEG_ATTR_32BIT 0x4u  /* D/B：32 位代码／栈 */
#define SEG_ATTR_PAGES 0x8u  /* G：limit 以 4 KiB 为单位 */

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

/* GDT 硬件格式，共 8 字节。字段顺序对应内存中的低地址到高地址。
 * GCC/x86 小端位域：一个字节内先声明的字段占低位。
 * access 保存 P/DPL/S/type；attr 保存 G/D/B/L/AVL，不属于 access。
 */
typedef struct _SEG_DESC {
    WORD limit_low;          /* +0：limit[15:0] */
    WORD base_low;           /* +2：base[15:0] */
    BYTE base_mid;           /* +4：base[23:16] */
    BYTE access;             /* +5：P(7), DPL(6:5), S(4), type(3:0) */
    BYTE limit_high : 4;     /* +6 的低四位：limit[19:16] */
    BYTE attr : 4;           /* +6 的高四位：G(3), D/B(2), L(1), AVL(0) */
    BYTE base_high;          /* +7：base[31:24] */
} __attribute__((packed)) SEG_DESC, *PSEG_DESC;

_Static_assert(sizeof(SEG_DESC) == 8, "GDT descriptor hardware format");

typedef struct _GDTR {
    WORD limit;
    DWORD base;
} __attribute__((packed)) GDTR, *PGDTR;

_Static_assert(sizeof(GDTR) == 6, "GDTR hardware format");
typedef struct _SMP_SEG {
    DWORD base;
    DWORD limit;
    DWORD flag;             /* access 字节，使用 GDT_ACC_* */
    DWORD attr;             /* 属性四位，使用 SEG_ATTR_*；TSS 为 0 */
} SMP_SEG, *PSMP_SEG;

/* IDT 门的硬件格式；任务门的 offset_low/high 都必须为 0。
 * type：门类型；attr：P/DPL/保留位组成的高四位。
 */
typedef struct _INT_DESC {
    WORD offset_low;        /* +0：入口 offset[15:0] */
    WORD sel;               /* +2：代码段选择子，任务门则为 TSS 选择子 */
    BYTE reserved;          /* +4：必须为 0 */
    BYTE type : 4;          /* +5 的低四位：INT_* 门类型 */
    BYTE attr : 4;          /* +5 的高四位：P(3), DPL(2:1), 保留(0) */
    WORD offset_high;       /* +6：入口 offset[31:16] */
} __attribute__((packed)) INT_DESC, *PINT_DESC;

_Static_assert(sizeof(INT_DESC) == 8, "IDT descriptor hardware format");

#define INT_RING_0 0x8
#define INT_RING_1 0xa
#define INT_RING_2 0xc
#define INT_RING_3 0xe

#define INT_TASK_GATE 0x5
#define INT_INT16     0x6
#define INT_TRAP16    0x7
#define INT_INT32     0xe
#define INT_TRAP32    0xf
#define INT_MAX       256

typedef struct _SMP_INT {
    WORD sel;
    BYTE type;
    BYTE attr;
    DWORD offset;
} SMP_INT, *PSMP_INT;

/* IDTR 是 IDT 的表寄存器格式；LDTR 是 LDT 选择子，不能混用命名。 */
typedef struct _IDTR {
    WORD limit;
    DWORD base;
} __attribute__((packed)) IDTR, *PIDTR;

_Static_assert(sizeof(IDTR) == 6, "IDTR hardware format");

void int_xlat_read(PINT_DESC src, PSMP_INT dest);
void int_xlat_write(PINT_DESC dest, PSMP_INT src);

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

void seg_xlat(PSMP_SEG dest, PSEG_DESC src);
void seg_xlat_in(PSMP_SEG src, PSEG_DESC dest);
/* 普通 32 位分页粒度段的便捷写入；TSS/16 位段用 seg_xlat_in 指定 attr。 */
void seg_write(PSEG_DESC dest, DWORD base, DWORD limit, BYTE flags);

#define TASK_MAX 64

#endif
