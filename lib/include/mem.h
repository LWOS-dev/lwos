#ifndef _MEM_H
#define _MEM_H

#include "stdint.h"
#include "bitmap.h"

#define MMAP_BUF    0x8000
#define MMAP_MAGIC  0x53445241
#define MMAP_MAX    128

#define ARDS_USABLE     1
#define ARDS_RESERVED   2
#define ARDS_ACPI_REC   3
#define ARDS_ACPI_NVS   4
#define ARDS_BAD        5

typedef struct _ARDS_T {
    QWORD base;
    QWORD len;
    DWORD type;
} ARDS_T, PARDS_T;
typedef const ARDS_T* PCARDS_T;

PVOID memcpy(PVOID dst, PCVOID src, int count);
PVOID memzero(PVOID dst, int len);

void mmap_init(void);
void mmap_dump(void);

typedef struct _MEM_POOL {
    DWORD base;
    DWORD unit_size; // 0=idle
} MEM_POOL, *PMEM_POOL;

void mem_pool_init();
void mem_pool_dump(PMEM_POOL pool);
PVOID mem_alloc_units(PMEM_POOL pool, DWORD count);

/* Single-CPU allocator critical section. Hosted regression binaries have no
 * hardware IRQs; freestanding OS builds must preserve the caller's IF. */
static inline DWORD mem_irq_save(void) {
#if !__STDC_HOSTED__
    DWORD flags;
    asm volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
#else
    return 0;
#endif
}
static inline void mem_irq_restore(DWORD flags) {
#if !__STDC_HOSTED__
    asm volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
#else
    (void)flags;
#endif
}

/* Page-granular allocation; free accepts only the original base address. */
PVOID kmalloc(int size);
void kfree(PVOID ptr);

#endif