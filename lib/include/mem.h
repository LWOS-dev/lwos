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

PVOID kmalloc(int size);

#endif