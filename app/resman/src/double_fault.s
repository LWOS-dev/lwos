bits 32

section .text
global double_fault_entry

; IDT task gate 已切到 DF TSS 的独立栈，并压入一个 DWORD 错误码（恒为 0）。
; 不使用普通 ISR 的寄存器帧，也不执行 iret 返回到故障任务。
double_fault_entry:
    cli
    cld
    add esp, 4              ; 丢弃 DF 错误码

    ; 直接写 VGA 文本显存，避免依赖 ABI、分配器或故障任务的控制台状态。
    ; 当前 DF TSS 的 DS/ES 使用 base=0 的平坦数据段。
    mov esi, df_message
    mov edi, 0xb8000
    mov ah, 0x4f            ; 白字红底
.print:
    lodsb
    test al, al
    jz .halt
    stosw
    jmp .print
.halt:
    hlt
    jmp .halt

section .rodata
df_message: db 'DOUBLE FAULT (#DF) - SYSTEM HALTED', 0

section .note.GNU-stack noalloc noexec nowrite progbits
