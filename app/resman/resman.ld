ENTRY(resman_main)

SECTIONS
{
    . = 0x180000;
    .rmtab : { KEEP(*(.rmtab)) }
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }

    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    __bss_end = .;

    ASSERT(ADDR(.rmtab) == 0x180000, "resman header must start at 0x180000")
    ASSERT(SIZEOF(.rmtab) == 0x43 * 4, "resman slot table size mismatch")
    ASSERT(__bss_end <= 0x200000, "resman overlaps monitor")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
