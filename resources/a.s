# SMCE: AT&T syntax, 32-bit machine code
.org 0
start:
    movl $0x12345678, %eax
    movw $0xffff, %cx
    movb $1, %dl
    movl %eax, -4(%ebp)
    movl -4(%ebp), %ebx
    movl $end, %esi
    jmp start
end:
    .long start
