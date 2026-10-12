/* 宿主测试：直接对照 x86 描述符的硬件字节布局，不执行 lgdt/lidt。 */
#include <assert.h>
#include <stdio.h>
#include "task.h"

static void expect_bytes(PCVOID descriptor, PCBYTE expected, int count)
{
    PCBYTE bytes = descriptor;
    for (int i = 0; i < count; i++) assert(bytes[i] == expected[i]);
}

int main(void)
{
    SEG_DESC segment;
    SMP_SEG values = {
        .base = 0x12345678, .limit = 0xabcde,
        .flag = GDT_ACC_DATA_RW, .attr = SEG_ATTR_32BIT | SEG_ATTR_PAGES,
    };
    const BYTE segment_bytes[] = {0xde, 0xbc, 0x78, 0x56, 0x34, 0x92, 0xca, 0x12};
    seg_xlat_in(&values, &segment);
    expect_bytes(&segment, segment_bytes, sizeof(segment));
    SMP_SEG decoded;
    seg_xlat(&decoded, &segment);
    assert(decoded.base == values.base && decoded.limit == values.limit);
    assert(decoded.flag == values.flag && decoded.attr == values.attr);

    seg_write(&segment, 0, 0xfffff, GDT_ACC_CODE_RX);
    const BYTE flat32[] = {0xff, 0xff, 0, 0, 0, 0x9a, 0xcf, 0};
    expect_bytes(&segment, flat32, sizeof(segment));
    values = (SMP_SEG){.limit = 0xffff, .flag = GDT_ACC_DATA_RW};
    seg_xlat_in(&values, &segment);
    const BYTE flat16[] = {0xff, 0xff, 0, 0, 0, 0x92, 0, 0};
    expect_bytes(&segment, flat16, sizeof(segment));
    values = (SMP_SEG){.base = 0x12345678, .limit = sizeof(TSS32) - 1,
                       .flag = GDT_ACC_TSS32_AVAIL};
    seg_xlat_in(&values, &segment);
    const BYTE tss[] = {103, 0, 0x78, 0x56, 0x34, 0x89, 0, 0x12};
    expect_bytes(&segment, tss, sizeof(segment));

    INT_DESC gate;
    SMP_INT interrupt = {.sel = 0x28, .type = INT_INT32,
                         .attr = INT_RING_0, .offset = 0x12345678};
    int_xlat_write(&gate, &interrupt);
    const BYTE interrupt_bytes[] = {0x78, 0x56, 0x28, 0, 0, 0x8e, 0x34, 0x12};
    expect_bytes(&gate, interrupt_bytes, sizeof(gate));
    SMP_INT decoded_interrupt;
    int_xlat_read(&gate, &decoded_interrupt);
    assert(decoded_interrupt.offset == interrupt.offset);
    assert(decoded_interrupt.sel == interrupt.sel);
    assert(decoded_interrupt.type == interrupt.type);
    assert(decoded_interrupt.attr == interrupt.attr);
    interrupt.type = INT_TASK_GATE;
    int_xlat_write(&gate, &interrupt);
    const BYTE task_bytes[] = {0, 0, 0x28, 0, 0, 0x85, 0, 0};
    expect_bytes(&gate, task_bytes, sizeof(gate));

    SEG_DESC table[SEG_MAX] = {0};
    sys_gdt = table;
    assert(find_free_gdt() == 5);
    for (int i = 5; i < SEG_MAX; i++) table[i].access = GDT_ACC_TSS32_AVAIL;
    assert(find_free_gdt() == -1);
    puts("PASS: GDT/IDT hardware bytes, decode, TSS attributes, free slots");
    return 0;
}
