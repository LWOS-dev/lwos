#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "bitmap.h"

/* Independent reference: check every possible candidate and requested bit. */
static int expected_run(unsigned pattern, int n, int start, int count)
{
    if (start < 0 || start >= n || count <= 0 || count > n - start)
        return -1;
    for (int p = start; p <= n - count; p++) {
        int j;
        for (j = 0; j < count; j++)
            if ((pattern >> (p + j)) & 1u) break;
        if (j == count) return p;
    }
    return -1;
}

int main(void)
{
    for (int n = 0; n <= 10; n++) {
        for (unsigned pattern = 0; pattern < (1u << n); pattern++) {
            /* Guard bytes and set padding bits catch writes outside the range. */
            BYTE storage[] = {0xa5, (BYTE)pattern,
                              (BYTE)((pattern >> 8) | 0xfc), 0x5a};
            BITMAP bm = {storage + 1, (DWORD)n};
            for (int start = -1; start <= n + 1; start++) {
                int valid = start >= 0 && start < n;
                assert(bitmap_test(&bm, start) ==
                       (valid ? (int)((pattern >> start) & 1u) : 0));
                for (int value = 0; value <= 1; value++) {
                    int next = -1, length = -1;
                    if (valid) {
                        length = 0;
                        while (start + length < n &&
                               (int)((pattern >> (start + length)) & 1u) == value)
                            length++;
                        for (int p = start; p < n; p++) {
                            if ((int)((pattern >> p) & 1u) == value) {
                                next = p;
                                break;
                            }
                        }
                    }
                    assert(bitmap_find_next(&bm, start, value) == next);
                    assert(bitmap_run_length(&bm, start, value) == length);
                }
                for (int count = -1; count <= n + 2; count++) {
                    assert(bitmap_find_zero_run(&bm, start, count) ==
                           expected_run(pattern, n, start, count));
                    for (int value = 0; value <= 1; value++) {
                        BYTE copy[4];
                        for (int b = 0; b < 4; b++) copy[b] = storage[b];
                        BITMAP target = {copy + 1, (DWORD)n};
                        if (value) bitmap_set_range(&target, start, count);
                        else bitmap_clear_range(&target, start, count);
                        int range_valid = start >= 0 && start <= n &&
                                          count >= 0 && count <= n - start;
                        for (int bit = 0; bit < 16; bit++) {
                            int expected = (storage[1 + bit / 8] >> (bit % 8)) & 1;
                            if (range_valid && bit >= start && bit - start < count)
                                expected = value;
                            assert(((copy[1 + bit / 8] >> (bit % 8)) & 1) == expected);
                        }
                        assert(copy[0] == 0xa5 && copy[3] == 0x5a);
                    }
                }
            }
        }
    }

    BYTE data = 0x55;
    BITMAP bm = {&data, 8};
    bitmap_set_range(&bm, 1, INT_MAX);
    bitmap_clear_range(&bm, INT_MAX, INT_MAX);
    bitmap_set(&bm, 8);
    bitmap_clear(&bm, -1);
    assert(data == 0x55);
    assert(bitmap_find_zero_run(&bm, 1, INT_MAX) == -1);
    assert(bitmap_find_zero_run(0, 0, 1) == -1);
    puts("bitmap tests passed (all patterns up to 10 bits)");
    return 0;
}
