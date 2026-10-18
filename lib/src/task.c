#include "task.h"

/* 无状态的描述符编解码；运行时任务和表状态由 RESMAN 持有。 */
void seg_xlat(PSMP_SEG dest, PSEG_DESC src) {
    dest->base = src->base_low |
                 ((DWORD)src->base_mid << 16) |
                 ((DWORD)src->base_high << 24);
    dest->limit = src->limit_low | ((DWORD)src->limit_high << 16);
    dest->flag = src->access;
    dest->attr = src->attr;
}

void seg_xlat_in(PSMP_SEG src, PSEG_DESC dest) {
    dest->base_low = (WORD)src->base;
    dest->base_mid = (BYTE)(src->base >> 16);
    dest->base_high = (BYTE)(src->base >> 24);
    dest->limit_low = (WORD)src->limit;
    dest->limit_high = src->limit >> 16;
    dest->access = (BYTE)src->flag;
    dest->attr = src->attr;
}

void seg_write(PSEG_DESC dest, DWORD base, DWORD limit, BYTE flags) {
    SMP_SEG segment = {
        .base = base,
        .limit = limit,
        .flag = flags,
        .attr = SEG_ATTR_32BIT | SEG_ATTR_PAGES,
    };
    seg_xlat_in(&segment, dest);
}

void int_xlat_read(PINT_DESC src, PSMP_INT dest) {
    dest->offset = src->offset_low | ((DWORD)src->offset_high << 16);
    dest->sel = src->sel;
    dest->attr = src->attr;
    dest->type = src->type;
}

void int_xlat_write(PINT_DESC dest, PSMP_INT src) {
    /* 任务门不携带入口 offset；CS:EIP 由目标 TSS 提供。 */
    DWORD offset = src->type == INT_TASK_GATE ? 0 : src->offset;
    dest->offset_low = (WORD)offset;
    dest->offset_high = (WORD)(offset >> 16);
    dest->sel = src->sel;
    dest->reserved = 0;
    dest->attr = src->attr;
    dest->type = src->type;
}
