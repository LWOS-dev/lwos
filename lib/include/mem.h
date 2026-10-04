#ifndef _MEM_H
#define _MEM_H

#include "stdint.h"

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

#endif