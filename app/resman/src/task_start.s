bits 32

section .text
global task_start
extern task_bootstrap

task_start:
    clts
    cld
    and esp, -16
    call task_bootstrap
.hang:
    cli
    hlt
    jmp .hang