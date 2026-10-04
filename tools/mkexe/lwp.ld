/* ld -m elf_i386 --emit-relocs -T tools/mkexe/lwp.ld ...
 * 编译使用 -fno-pie -fno-pic; 入口接收 LW_ENV 的约定见 spec2/40。
 */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(lwp_main)
PHDRS { image PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0;
    .text : { *(.text.start) *(.text .text.*) } :image
    .rodata ALIGN(16) : { *(.rodata .rodata.*) } :image
    .data ALIGN(16) : { *(.data .data.*) } :image
    .bss ALIGN(16) (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :image
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
