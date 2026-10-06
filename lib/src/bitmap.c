#include "bitmap.h"
#include "stdint.h"

int bitmap_test(PCBITMAP bm, int pos) {
    if (!bm || pos<0 || (DWORD)pos>=bm->nbits)return 0;
    return (bm->data[pos>>3]&(1<<(pos&7)))?1:0;
}
void bitmap_set(PBITMAP bm, int pos) {
    if (!bm || pos<0 || (DWORD)pos>=bm->nbits)return;
    bm->data[pos>>3]|=(1<<(pos&7));
}
void bitmap_clear(PBITMAP bm, int pos) {
    if (!bm || pos<0 || (DWORD)pos>=bm->nbits)return;
    bm->data[pos>>3]&=~(1<<(pos&7));
}
void bitmap_set_range(PBITMAP bm, int start, int count) {
    if (!bm)return;
    if (start<0 || (DWORD)start>bm->nbits || count<0)return;
    if ((DWORD)count>bm->nbits-(DWORD)start)return;
    for (int i=0; i<count; i++) {
        bitmap_set(bm,start+i);
    }
}
void bitmap_clear_range(PBITMAP bm, int start, int count) {
    if (!bm)return;
    if (start<0 || (DWORD)start>bm->nbits || count<0)return;
    if ((DWORD)count>bm->nbits-(DWORD)start)return;
    for (int i=0; i<count; i++) {
        bitmap_clear(bm,start+i);
    }
}

int bitmap_find_next(PCBITMAP bm, int start, int value) {
    if (!bm || start<0 || (DWORD)start>=bm->nbits)return -1;
    for (int i=start; (DWORD)i<bm->nbits; i++) {
        if (bitmap_test(bm, i)==(value&1))return i;
    }
    return -1;
}
int bitmap_run_length(PCBITMAP bm, int start, int value) {
    if (!bm || start<0 || (DWORD)start>=bm->nbits)return -1;
    for (int i=start; (DWORD)i<bm->nbits; i++) {
        if (bitmap_test(bm, i)!=(value&1)) {
            return i-start;
        }
    }
    return (int)(bm->nbits-(DWORD)start);
}

int bitmap_find_zero_run(
    PCBITMAP bm,
    int start,
    int count
) {
    int i=start;
    int p,l;

    if (!bm || start<0 || (DWORD)start>=bm->nbits || count<=0)
        return -1;
    if ((DWORD)count>bm->nbits-(DWORD)start)return -1;

    while ((DWORD)i<bm->nbits) {
        p=bitmap_find_next(bm, i, 0);
        if (p<0)return -1;
        if ((DWORD)count>bm->nbits-(DWORD)p)return -1;

        l=bitmap_run_length(bm, p, 0);
        if (l>=count)return p;
        if (l<=0)return -1;
        i=p+l;
    }
    return -1;
}
