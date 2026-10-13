bits 32

section .text
extern resman_isr_dispatch

; 每个向量只补齐 [vector, error]，保存与恢复共用同一个 stub。
%macro ISR 1
resman_isr%1:
    ; 这些异常的错误码已经由 CPU 压栈；其余向量补一个零。
    %if %1 != 8 && %1 != 10 && %1 != 11 && %1 != 12 && %1 != 13 && %1 != 14 && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
        push dword 0
    %endif
    push strict dword %1
    jmp isr_common
%endmacro

%assign vector 0
%rep 256
    ISR vector
    %assign vector vector + 1
%endrep

isr_common:
    pushad
    ; 显式保存为 DWORD，避免 push 段寄存器留下未定义的高 16 位。
    xor eax, eax
    mov ax, ds
    push eax
    mov ax, es
    push eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld

    ; i386 C 调用前对齐栈；EBX 保存原帧位置，由 C 按 ABI 保留。
    mov ebx, esp
    and esp, -16
    sub esp, 12
    push ebx
    call resman_isr_dispatch
    mov esp, ebx

    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
    add esp, 8              ; 丢弃 vector 和 error
    iretd

section .rodata
align 4
global resman_isr_table
resman_isr_table:
%assign vector 0
%rep 256
    dd resman_isr%+vector
    %assign vector vector + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
