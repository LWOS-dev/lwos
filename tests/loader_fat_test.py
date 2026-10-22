#!/usr/bin/env python3
"""Run the loader's actual FAT lookup code against a synthetic fragmented disk.
Only BIOS reads and console output are stubbed; no guest image is modified.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'loader/loader.c').read_text()
fat_code = source[source.index('static uint8_t fat_buf[512];'):
                  source.index('void load_cluster_to_memory(')]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t *uint8_p;
typedef uint16_t *uint16_p;
typedef uint32_t *uint32_p;
#define puts(s) ((void)(s))
#define putc(c) ((void)(c))
#define put_dword(n) ((void)(n))
#define memzero(p,n) memset(p,0,n)
static const char _hex[] = "0123456789ABCDEF";
static uint8_t disk[512][512];
static unsigned reads;
static void disk_read_checked(uint32_t lba, unsigned count, uint16_p dst) {
    assert(count == 1 && lba < 512);
    assert(++reads < 200); /* Detect non-terminating lookup. */
    memcpy(dst, disk[lba], 512);
}
'''
cases = r'''
static void link_cluster(unsigned cluster, uint32_t next) {
    memcpy(disk[2 + cluster / 128] + cluster % 128 * 4, &next, 4);
}
static uint8_t *entry(unsigned cluster, unsigned slot) {
    return disk[cluster_to_lba(cluster)] + slot * 32;
}
static void file(unsigned cluster, unsigned slot, const char *name, uint8_t attr) {
    uint8_t *p = entry(cluster, slot);
    memset(p, 0, 32);
    memcpy(p, name, 11);
    p[11] = attr;
    p[20] = 1; /* Exercise the high cluster word too. */
    p[26] = 42;
    p[28] = 123;
}
static void setup(void) {
    memset(disk, 0, sizeof(disk));
    reads = 0;
    root_cluster = 2;
    start_lba = 10;
    mbrpte[0].lba = 1;
    part_entry = 0;
    reserved_sector = 1;
    /* Deliberately jump across a FAT-sector boundary, then backwards. */
    link_cluster(2, 0xa0000082);
    link_cluster(130, 7);
    link_cluster(7, 0x0fffffff);
    for (unsigned i = 0; i < 16; i++) {
        file(2, i, "FILLER  TXT", 0x20);
        file(130, i, "FILLER  TXT", 0x20);
        file(7, i, "FILLER  TXT", 0x20);
    }
}
static void expect(const char *name, int found) {
    uint32_t cluster = 99, size = 99;
    reads = 0;
    search_file(name, &cluster, &size);
    assert(cluster == (found ? 0x1002a : 0));
    assert(size == (found ? 123 : 0));
}
int main(void) {
    const char *names[] = {"BOOT    INI", "ABI     BIN", "MONITOR BIN"};
    for (unsigned n = 0; n < 3; n++) {
        setup(); file(2, 0, names[n], 0x20); expect(names[n], 1);
        setup(); file(2, 15, names[n], 0x20); expect(names[n], 1);
        setup(); file(130, 0, names[n], 0x20); expect(names[n], 1);
        setup(); file(7, 15, names[n], 0x20); expect(names[n], 1);
    }
    setup(); expect("MISSING BIN", 0); /* Full last cluster, no 0x00 entry. */
    setup(); entry(2, 5)[0] = 0; file(130, 0, "MONITOR BIN", 0x20);
    expect("MONITOR BIN", 0); /* End marker must stop the whole scan. */
    setup(); file(2, 0, "MONITOR BIN", 0x10); /* Directory impostor. */
    file(2, 1, "MONITOR BIN", 0x08); /* Volume label. */
    file(2, 2, "MONITOR BIN", 0x0f); /* LFN slot. */
    file(2, 3, "MONITOR BIN", 0x20); entry(2, 3)[0] = 0xe5;
    expect("MONITOR BIN", 0);
    file(130, 0, "MONITOR BIN", 0x21); expect("MONITOR BIN", 1);
    const uint32_t stops[] = {0, 1, 0x0ffffff0, 0x0ffffff7, 0x0ffffff8, 0x0fffffff};
    for (unsigned i = 0; i < sizeof(stops)/sizeof(stops[0]); i++) {
        setup(); link_cluster(2, stops[i]); expect("MONITOR BIN", 0);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='lwos-loader-fat-') as tmp:
    src = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    src.write_text(harness + fat_code + cases)
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: loader FAT lookup: entries 1/16/17/48, fragmented chain, filters and termination')
