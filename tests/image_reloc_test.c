/* Host regression: no privileged instructions or application entry execution. */
#include <assert.h>
#include <stdio.h>
#include "../app/resman/include/resman.h"
#include "mem.h"

static BYTE storage[4][128];
static unsigned allocations, frees;
static int fail_at=-1;
static PVOID failure_value;
PVOID kmalloc(int size) {
    if ((int)allocations==fail_at)return failure_value;
    assert(size <= 128 && allocations < 4);
    return storage[allocations++];
}
void kfree(PVOID p) { assert(p); frees++; }
PVOID memcpy(PVOID dst, PCVOID src, int count) {
    for (int i=0; i<count; i++) ((PBYTE)dst)[i]=((PCBYTE)src)[i];
    return dst;
}
PVOID memzero(PVOID dst, int count) {
    for (int i=0; i<count; i++) ((PBYTE)dst)[i]=0;
    return dst;
}
static DWORD read32(PCVOID p) {
    DWORD value;
    memcpy(&value,p,4);
    return value;
}
int main(void) {
    struct { LwpHdr hdr; BYTE data[33]; } file={0};
    file.hdr=(LwpHdr){.magic=LWP_MAGIC, .version=LWP_VERSION,
        .hdr_size=LWP_HEADER_SIZE, .image_off=64, .image_size=17,
        .mem_size=32, .stack_size=64, .reloc_off=81, .reloc_count=2};
    DWORD value=12, rva=9;
    memcpy(file.data+1,&value,4);
    value=4;
    memcpy(file.data+9,&value,4);
    memcpy(file.data+17,&rva,4); /* Unsorted table; unaligned table and targets. */
    rva=1;
    memcpy(file.data+21,&rva,4);
    PRM_IMAGE first, second;
    assert(resman_image_load(&file,89,1,&first)==RM_OK);
    assert(resman_image_load(&file,89,2,&second)==RM_OK);
    assert(first->base!=second->base);
    PRM_IMAGE loaded[2]={first,second};
    for (int n=0; n<2; n++) {
        PBYTE base=loaded[n]->base;
        assert(read32(base+1)==(DWORD)base+12);
        assert(read32(base+9)==(DWORD)base+4);
        for (int i=17; i<32; i++) assert(base[i]==0);
        assert(loaded[n]->state==RM_IMAGE_LOADED);
    }
    assert(read32(file.data+1)==12 && read32(file.data+9)==4);
    assert(resman_image_unload(first)==RM_OK);
    assert(resman_image_unload(second)==RM_OK);
    DWORD invalid[]={9,10,14,0xffffffffu};
    for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
        memcpy(file.data+21,&invalid[i],4);
        first=(PRM_IMAGE)1;
        assert(resman_image_load(&file,89,0,&first)==-10 && first==0);
        assert(allocations==4); /* Rejected before allocating. */
    }
    assert(resman_image_load(&file,88,0,&first)==-10);
    assert(resman_image_load(&file,8,0,&first)==-1 && first==0);
    allocations=0;
    file.hdr.reloc_count=0;
    file.hdr.reloc_off=0;
    assert(resman_image_load(&file,81,0,&first)==RM_OK);
    assert(read32((PBYTE)first->base+1)==12);
    assert(resman_image_unload(first)==RM_OK);
    for (int n=0;n<3;n++) {
        failure_value=n==0 ? 0 : (n==1 ? (PVOID)-1 : (PVOID)-2);
        for (fail_at=0;fail_at<2;fail_at++) {
            allocations=frees=0;
            assert(resman_image_load(&file,81,0,&first)==RM_ERR_MEMORY && !first);
            assert(frees==(unsigned)fail_at);
        }
    }
    fail_at=-1;
    for (int n=0;n<40;n++) {
        allocations=frees=0;
        assert(resman_image_load(&file,81,0,&first)==RM_OK);
        assert(resman_image_unload(first)==RM_OK && frees==2);
    }
    file.hdr.stack_size=0;
    assert(resman_image_load(&file,81,0,&first)==RM_ERR_ARGUMENT && !first);
    file.hdr.stack_size=0xffffffffu;
    assert(resman_image_load(&file,81,0,&first)==RM_ERR_ARGUMENT && !first);
    puts("PASS: relocation at two bases, unaligned/unsorted entries, BSS, invalid tables, empty table");
    return 0;
}
