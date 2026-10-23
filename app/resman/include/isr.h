#ifndef _LW_RM_ISR_H
#define _LW_RM_ISR_H

#include "stdint.h"

/* isr_common 保存后的 ring 0 栈帧，按低地址到高地址排列。
 * 同级中断没有额外的 SS/ESP；esp_before_pushad 指向 vector 字段。
 */
typedef struct _ISR_FRAME {
    DWORD gs, fs, es, ds;
    DWORD edi, esi, ebp, esp_before_pushad;
    DWORD ebx, edx, ecx, eax;
    DWORD vector, error;
    DWORD eip, cs, eflags;
} ISR_FRAME, *PISR_FRAME;

_Static_assert(sizeof(ISR_FRAME) == 68, "ISR ring 0 frame");
_Static_assert(__builtin_offsetof(ISR_FRAME, vector) == 48, "ISR vector offset");
_Static_assert(__builtin_offsetof(ISR_FRAME, eip) == 56, "ISR EIP offset");

/* 回调可修改保存的寄存器帧；以后 IRQ 回调还需负责设备处理和 PIC EOI。
 * 当前全部 ring 0，PIC IRQ 仍屏蔽；本接口不自动开启硬件中断。
 */
typedef void (*ISR_HANDLER)(PISR_FRAME frame);

extern const DWORD resman_isr_table[256];
int resman_isr_set_handler(DWORD vector, ISR_HANDLER handler);
ISR_HANDLER resman_isr_get_handler(DWORD vector);
void resman_isr_dispatch(PISR_FRAME frame);

void pic_init(void);
void pic_irq_set(DWORD irq, int enable);


#endif
