ENTRY(monitor_main)

STACK_SIZE = 16K;

SECTIONS
{
    . = 0x200000;

    __abi_start = .;
    .abi : { *(.abi) }
    __abi_end = .;

    .text : {
        *(.text.start)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    . = . + STACK_SIZE;
    __stack_top = .;
    __bss_end = .;

    ASSERT(__bss_end <= 0x350000, "MONITOR overlaps font/fixed system memory")
    ASSERT(__abi_start == 0x200000, "映像头没有落在 0x200000")
    ASSERT(__abi_end > __abi_start, "映像头是空的，head.o 没链进来")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }

}