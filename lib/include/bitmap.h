#ifndef _BITMAP_H
#define _BITMAP_H

#include "stdint.h"

typedef struct _BITMAP {
    BYTE  *data;
    DWORD nbits; /* valid bits; must fit in int for this API */
} BITMAP, *PBITMAP;

typedef const BITMAP *PCBITMAP;

// 1 or 0
int bitmap_test(PCBITMAP bm, int pos);

void bitmap_set(PBITMAP bm, int pos);
void bitmap_clear(PBITMAP bm, int pos);

// [start, start + count); invalid ranges are ignored; count == 0 is a no-op
void bitmap_set_range(PBITMAP bm, int start, int count);
void bitmap_clear_range(PBITMAP bm, int start, int count);

// find value from start (inclusive), return pos; -1 if invalid or not found
int bitmap_find_next(PCBITMAP bm, int start, int value);

// bits from start continous equals to value
// starts at !value return 0; reaching the end returns the remaining length
// invalid start returns -1
int bitmap_run_length(PCBITMAP bm, int start, int value);

// first run of at least count zeros; count must be positive; -1 on failure
int bitmap_find_zero_run(PCBITMAP bm,
                            int start, int count);


#endif
