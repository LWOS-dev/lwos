#include "task.h"

PSEG_DESC sys_gdt;
GDTR sys_gdtr;

void seg_xlat(PSMP_SEG dest, PSEG_DESC src) {
    DWORD base=0,limit=0;
    base=(((*(PQWORD)src)>>16)&0xFFFFFF) |
        (((*(PQWORD)src)>>32)&0xFF000000);
    limit=((*(PQWORD)src)&0xFFFF) | 
        (((*(PQWORD)src)>>32)&0xF0000);
    dest->base=base;
    dest->limit=limit;
}
void seg_xlat_in(PSMP_SEG src, PSEG_DESC dest) {
    *dest=
        (((QWORD)(src->base)&0x00FFFFFF)<<16) |
        (((QWORD)(src->base)&0xFF000000)<<32) |
        (((QWORD)(src->limit)&0x0000FFFF)) |
        (((QWORD)(src->limit)&0x000F0000)<<32) |
        SEG_FLAGS | 
        (((QWORD)(src->flag)&0xFF)<<40);
}
void seg_write(PSEG_DESC dest, DWORD base, DWORD limit, BYTE flags) {
    *dest=
        (((QWORD)base&0x00FFFFFF)<<16) |
        (((QWORD)base&0xFF000000)<<32) |
        (((QWORD)limit&0x0000FFFF)) |
        (((QWORD)limit&0x000F0000)<<32) |
        SEG_FLAGS | 
        (((QWORD)flags&0xFF)<<40);
}
void gdt_update() {
    asm volatile("lgdt %0" : : "m"(sys_gdtr));
}
void gdt_init() {
    sys_gdt=(PSEG_DESC)0x360000;
    sys_gdtr.base=(DWORD)sys_gdt;
    sys_gdtr.limit=(SEG_MAX<<3)-1;
    memzero(sys_gdt, 0x10000);
    *(PQWORD)(sys_gdt+1)=0x00cf9a000000ffff;
    *(PQWORD)(sys_gdt+2)=0x00cf92000000ffff;
    *(PQWORD)(sys_gdt+3)=0x00009a000000ffff;
    *(PQWORD)(sys_gdt+4)=0x000092000000ffff;
    gdt_update();
}
