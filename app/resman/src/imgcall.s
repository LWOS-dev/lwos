[bits 32]

section .text
global image_call

; int image_call(DWORD entry, DWORD stack_top, PCLW_ENV env)
; [ebp + 8]  = entry
; [ebp + 12] = stack_top
; [ebp + 16] = env

image_call:
    push ebp
    mov ebp, esp

    push ebx
    push esi
    push edi
    pushfd

    mov eax, [ebp+8]
    mov edx, [ebp+12]
    mov ecx, [ebp+16]

    mov esp, edx
    and esp, -16

    sub esp, 12
    push ecx

    cld
    call eax

    lea esp, [ebp-16]

    popfd
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits