#ifndef EXTERNAL_FPU_CHECK_H
#define EXTERNAL_FPU_CHECK_H

#include "abi.h"
#include "resman.h"
#include "task.h"

/* 32-bit protected-mode FSAVE layout. Reserved halves of environment words
 * and instruction/data pointers are deliberately excluded from comparisons. */
typedef struct {
    WORD control, reserved0;
    WORD status, reserved1;
    WORD tag, reserved2;
    DWORD ip, cs_opcode, data, ds;
    BYTE st[8][10];
} FPU_CHECK_STATE;

_Static_assert(sizeof(FPU_CHECK_STATE) == 108, "32-bit x87 save image");

static int fpu_check(PTASK self, DWORD variant) {
    FPU_CHECK_STATE expected = {0}, observed;
    expected.control = variant == 1 ? 0x037f : 0x0b7f;
    /* Different TOP, condition flags, rounding modes and all eight values.
     * Each ST(i) is an exact 80-bit power of two: A positive, B negative.
     * Construct the reference in integer code so a broken FPU cannot teach
     * the test an already-corrupted reference value. All eight tags = valid. */
    expected.status = variant == 1 ? 0 : (3u << 11) | 0x0100;
    for (DWORD i = 0; i < 8; i++) {
        WORD exponent = 0x3fff + i + (variant == 1 ? 0 : 8);
        if (variant == 2) exponent |= 0x8000;
        expected.st[i][7] = 0x80;
        expected.st[i][8] = (BYTE)exponent;
        expected.st[i][9] = (BYTE)(exponent >> 8);
    }
    asm volatile("frstor %0" :: "m"(expected) : "memory");
    for (;;) {
        /* Keep the entire x87 stack live while PIT preempts us. No ABI calls,
         * yield or interrupt masking, and no compiler-generated FP code. */
        for (volatile DWORD delay = 0; delay < 65536; delay++)
            asm volatile("" ::: "memory");
        asm volatile("fnsave %0; frstor %0" : "=m"(observed) :: "memory");
        DWORD error = 0;
        if (observed.control != expected.control) error = 1;
        else if (observed.status != expected.status) error = 2;
        else if (observed.tag != expected.tag) error = 3;
        else {
            for (DWORD i = 0; i < 8 && !error; i++)
                for (DWORD j = 0; j < 10; j++)
                    if (observed.st[i][j] != expected.st[i][j]) {
                        error = 0x10 + i;
                        break;
                    }
        }
        if (error) {
            self->work_count = 0x80000000u | error;
            asm volatile("fninit" ::: "memory");
            return -(int)error;
        }
        self->work_count += variant;
    }
}

#endif
