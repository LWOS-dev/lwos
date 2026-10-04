#include "mem.h"
#include "stdint.h"
#include "abi.h"

PVOID memcpy(PVOID dst, PCVOID src, int count) {
    void * ret = dst;
    while (count--) {
        *(char *)dst = *(char *)src;
        dst = (char *)dst + 1;
        src = (char *)src + 1;
    }
    return ret;
}
PVOID memzero(PVOID dst, int len) {
    PBYTE p = (PBYTE)dst;
    while (len-- > 0) {
        *(p++)=0;
    }
    return dst;
}

static ARDS_T tab[MMAP_MAX];
static DWORD n_entries;
static DWORD flags;
static int inited;

void mmap_init(void) {
    volatile PCDWORD hdr = (PCDWORD)MMAP_BUF;
    PCARDS_T src = (PCARDS_T)(MMAP_BUF+16);
    DWORD cnt;

    if (inited)return;
    inited=1;

    if (hdr[0]!=MMAP_MAGIC)return;

    cnt=hdr[1];
    flags=hdr[2];

    if (cnt>MMAP_MAX)cnt=MMAP_MAX;

    for (DWORD i=0; i<cnt; i++) {
        tab[i]=src[i];
    }
    n_entries=cnt;
}
static QWORD mmap_usable(void) {
    QWORD total=0;
    for (DWORD i=0; i<n_entries; i++) {
        if (tab[i].type==1)total+=tab[i].len;
    }
    return total;
}
void mmap_dump(void) {
    QWORD base, len;

    if (!n_entries) {
        lw_puts("NO BIOS MEMORY MAP\n\r");
        return;
    }

    lw_puts("BIOS MEMORY MAP:\n\r");
    lw_puts("  ENTRIES COUNT=");
    lw_put_dword(n_entries);
    lw_puts("\n\r");
    if (flags&1) {
        lw_puts("**E801: RANGES ARE APPROX\n\r");
    }

    for (DWORD i=0; i<n_entries; i++) {
        PCARDS_T e=&tab[i];

        lw_put_byte((BYTE)i);
        lw_puts(" ");
        lw_put_qword(e->base);
        lw_puts(" + ");
        lw_put_qword(e->len);
        lw_puts("  ");
        lw_put_qword(e->base+e->len-1);
        lw_puts("  ");
        lw_put_byte(e->type);

        if (e->type==1) {
            lw_puts(" ");
            lw_put_dword(e->len>>10);
            lw_puts("H KB");
        }

        lw_puts("\n\r");
    }

    lw_puts("TOTAL ");
    lw_put_dword(mmap_usable()>>10);
    lw_puts("H KB");
    lw_puts("\n\r");
}