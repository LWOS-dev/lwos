/* Linux host test: project headers intentionally replace libc string/stdint.
 * Build with -fno-builtin; use only the few declared host I/O functions below.
 */
#include "fs.h"
#include "mem.h"
#include "string.h"

extern int open(const char *, int, ...);
extern long pread(int, void *, unsigned long, long);
extern long lseek(int, long, int);
extern int close(int);
extern int printf(const char *, ...);

static BYTE mbr[512], boot[512], fat[512];
static BYTE root[4][512], subdir[2][512];
static int fd = -1, fail_read;

static void put16(PBYTE p, WORD v) { p[0] = v; p[1] = v >> 8; }
static void put32(PBYTE p, DWORD v) { put16(p, v); put16(p + 2, v >> 16); }

static int test_read(PBLKDEV device, QWORD lba, DWORD count, PVOID out) {
    (void)device;
    if (fail_read || count != 1)
        return -1;
    if (fd >= 0)
        return pread(fd, out, 512, (long)(lba * 512)) == 512 ? 0 : -1;
    PCBYTE source = 0;
    if (lba == 0) source = mbr;
    else if (lba == 2048) source = boot;
    else if (lba == 2080) source = fat;
    else if (lba == 3680) source = root[0];
    else if (lba == 3681) source = root[1];
    else if (lba == 3694) source = root[2];
    else if (lba == 3695) source = root[3];
    else if (lba == 3690) source = subdir[0];
    else if (lba == 3691) source = subdir[1];
    if (!source)
        return -1;
    memcpy(out, source, 512);
    return 0;
}

static BLKDEV device = {
    .version = BLKDEV_VERSION, .size = sizeof(BLKDEV),
    .sector_size = 512, .sectors = 300000, .read = test_read
};

static void fixture(void) {
    put16(mbr + 510, 0xaa55);
    mbr[0x1be + 4] = 0x0c;
    put32(mbr + 0x1be + 8, 2048);
    put32(mbr + 0x1be + 12, 200000);
    put16(boot + 510, 0xaa55);
    put16(boot + 11, 512);
    boot[13] = 2; boot[16] = 2;
    put16(boot + 14, 32);
    put32(boot + 36, 800);
    put32(boot + 32, 200000);
    put32(boot + 44, 2);
    put32(fat + 2 * 4, 9); /* fragmented, two-cluster root directory */
    put32(fat + 9 * 4, 0x0fffffff);
    put32(fat + 7 * 4, 0x0fffffff);
    for (int s = 0; s < 4; s++)
        for (int i = 0; i < 16; i++) root[s][i * 32] = 0xe5;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < 16; i++) subdir[s][i * 32] = 0xe5;
    PBYTE e = root[2] + 3 * 32;
    memcpy(e, "RES        ", 11); e[11] = 0x10;
    put16(e + 26, 7); root[2][4 * 32] = 0;
    e = subdir[1] + 4 * 32;
    memcpy(e, "README  TXT", 11); e[11] = 0x20;
    put16(e + 20, 1); put16(e + 26, 2); put32(e + 28, 1234);
    subdir[1][5 * 32] = 0;
}

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

int main(int argc, char **argv) {
    FS_VOLUME first = {0}, second = {0};
    SFT_T e;
    DWORD cluster = 0xdeadbeef;
    CHECK(fs_lookup(&first, "/RES/README.TXT", &e) == FS_ERR_NOT_MOUNTED);
    CHECK(fs_mount(&first, 0) == FS_ERR_IO);
    fixture();
    CHECK(fs_mount(&first, &device) == 0);
    CHECK(fs_mount(&second, &device) == 0);
    CHECK(first.sectors_per_cluster == 2);
    CHECK(fs_lookup(&first, "/res/readme.txt", &e) == 0);
    CHECK(e.cluster == 65538 && e.length == 1234 && e.attr == 0x20);
    CHECK(fs_lookup(&second, "RES//README.TXT", &e) == 0);
    CHECK(resolve_path(&first, "/RES/README.TXT", &cluster) == 0 && cluster == 65538);
    CHECK(fs_lookup(&first, "/RES/", &e) == 0 && e.cluster == 7);
    CHECK(fs_lookup(&first, "/", &e) == 0 && e.cluster == 2);
    cluster = 0xdeadbeef;
    CHECK(resolve_path(&first, "/RES/MISSING.TXT", &cluster) == FS_PATH_ERROR);
    CHECK(cluster == 0xdeadbeef);
    CHECK(fs_lookup(&first, "/RES/README.TXT/CHILD", &e) == FS_PATH_ERROR);
    CHECK(fs_lookup(&first, "/RES/README.TXT/", &e) == FS_PATH_ERROR);
    CHECK(fs_lookup(&first, "/VERYLONGCOMPONENT/README.TXT", &e) == FS_PATH_ERROR);
    CHECK(fs_lookup(&first, "/RES/README.LONG", &e) == FS_PATH_ERROR);
    CHECK(fat_read_fte(&first, 0, 0, &e) == FS_ERR_FORMAT);
    fail_read = 1;
    CHECK(fs_lookup(&first, "/RES/README.TXT", &e) == FS_ERR_IO);
    fail_read = 0;
    put32(fat + 2 * 4, 0x0ffffff7);
    CHECK(fs_lookup(&first, "/RES/README.TXT", &e) == FS_ERR_FORMAT);
    put32(fat + 2 * 4, 2);
    first.cluster_count = 16;
    CHECK(fs_lookup(&first, "/MISSING.TXT", &e) == FS_ERR_FORMAT);
    put32(fat + 2 * 4, 9);
    CHECK(fs_mount(&first, 0) == FS_ERR_IO);
    CHECK(fs_lookup(&second, "/RES/README.TXT", &e) == 0);
    if (argc > 1) {
        fd = open(argv[1], 0);
        CHECK(fd >= 0);
        long image_size = lseek(fd, 0, 2);
        CHECK(image_size > 0 && image_size % 512 == 0);
        device.sectors = (QWORD)image_size / 512;
        CHECK(fs_mount(&first, &device) == 0);
        CHECK(fs_lookup(&first, "/RES/README.TXT", &e) == 0);
        CHECK(e.length > 0 && !(e.attr & 0x10));
        CHECK(fs_lookup(&first, "/BOOT.INI", &e) == 0);
        CHECK(close(fd) == 0);
    }
    printf("PASS: FAT32 volumes, fragmented directories, paths and errors\n");
    return 0;
}
