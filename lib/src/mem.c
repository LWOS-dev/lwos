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

#define MEM_POOL_MAX 512

PMEM_POOL mem_pool;
BITMAP mem_bm;

void mem_pool_init() { // init pool and bitmap
    mem_bm.data=(PBYTE)0x300000;
    mem_bm.nbits=0x100000;
    bitmap_set_range(&mem_bm,0,0x100000);

    for (DWORD i=0; i<n_entries; i++) {
        PCARDS_T e=&tab[i];
        if (e->type==1)
            bitmap_clear_range(&mem_bm,e->base/0x1000,e->len/0x1000);
    }
    bitmap_set_range(&mem_bm,0,1024);

    mem_pool=(PMEM_POOL)0x302000;
    memzero((PVOID)0x302000, 0x1000);
}

void mem_pool_dump(PMEM_POOL pool) {
    lw_puts("BASE=");
    lw_put_dword(pool->base);
    lw_puts(" LIMIT=");
    lw_put_dword(pool->unit_size*4096-1);
    lw_puts("\n\r");
}
PVOID mem_alloc_units(PMEM_POOL pool, DWORD count) {
    int pos;
    if (!pool || !count || count>mem_bm.nbits)return 0;
    pos = bitmap_find_zero_run(&mem_bm, 0, (int)count);
    if (pos<0)return 0;
    bitmap_set_range(&mem_bm, pos, (int)count);
    pool->base=pos;
    pool->unit_size=count;
    //mem_pool_dump(pool);
    return (PVOID)(pool->base);
}

PVOID kmalloc(int size) {
    if (size<0)return 0;
    int cnt=(size+4095)/4096;
 
    PVOID ptr=0;

    for (int i=0; i<MEM_POOL_MAX; i++) {
        if (mem_pool[i].unit_size==0) {
            ptr=mem_alloc_units(&mem_pool[i], cnt);
            if (!ptr) {
                return (PVOID)-1;
            }
            return ptr;
        }
    }
    lw_puts("OUT OF MEMORY\n\r");
    return (PVOID)-2; // out of memory
}
void kfree(PVOID ptr) {
    if (ptr<0)return;

    for (int i=0; i<MEM_POOL_MAX; i++) {
        if (mem_pool[i].unit_size==0)continue;
        if (
            ((int)ptr>=(mem_pool[i].base*4096)) &&
            ((int)ptr<(mem_pool[i].base*4096+mem_pool[i].unit_size*4096))
        ) {
            bitmap_clear_range(
                &mem_bm, mem_pool[i].base, mem_pool[i].unit_size
            );
        }
    }
}