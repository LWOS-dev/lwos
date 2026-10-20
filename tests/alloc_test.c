#include <assert.h>
#include <stdio.h>
#include "mem.h"

extern PMEM_POOL mem_pool;
extern BITMAP mem_bm;
static MEM_POOL pool[16384];
static BYTE bits[8];

int main(void) {
    mem_pool=pool;
    mem_bm.data=bits;
    mem_bm.nbits=64;
    bitmap_set(&mem_bm,0); /* reserve address zero */
    assert(!kmalloc(0) && !kmalloc(-1) && !kmalloc(0x7fffffff));
    PVOID first=kmalloc(1);
    PVOID second=kmalloc(4097);
    assert(first==(PVOID)4096 && second==(PVOID)8192);
    assert(pool[0].unit_size==1 && pool[1].unit_size==2);
    kfree((PBYTE)second+1);
    assert(pool[1].unit_size==2 && bitmap_test(&mem_bm,2));
    kfree(0);
    kfree(second);
    kfree(second);
    assert(!pool[1].unit_size && !bitmap_test(&mem_bm,2));
    PVOID reused=kmalloc(4097);
    assert(reused==second);
    kfree(reused);
    kfree(first);
    assert(bits[0]==1);
    for (unsigned i=1;i<sizeof(bits);i++)assert(!bits[i]);
    assert(kmalloc(64*4096)==(PVOID)-1);
    for (unsigned i=0;i<16384;i++)assert(!pool[i].unit_size);
    puts("PASS: page rounding, exact-base free, invalid sizes, exhaustion, reuse");
}
