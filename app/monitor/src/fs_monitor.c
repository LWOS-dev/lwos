/* monitor 的文件系统实验命令; FAT32 实现在 lib/src/fs.c。 */
#include "fs_monitor.h"
#include "abi.h"
#include "fs.h"

static void print_field(PCSTR s, int width) {
    for (int i = 0; i < width; i++)
        lw_putc(s[i]);
}
int fs_init(PFS_VOLUME fs) {
    lw_puts("FILESYSTEM DETECT\n\r");
    lw_disk_probe();
    int status = fs_mount(fs, lw_blk_get(0));
    if (!status)
        lw_puts("FAT32 READY\n\r");
    return status;
}

void fs_scan(PFS_VOLUME fs) {
    if (!fs || !fs->mounted) {
        lw_puts("FILESYSTEM NOT INITIALIZED; USE F FIRST\n\r");
        return;
    }
    SFT_T e;
    DWORD cur_cluster=fs->root_cluster;
    /* 限制遍历量, 避免循环 FAT 链使扫描永远不结束。 */
    QWORD max_entries = (QWORD)fs->cluster_count * fs->sectors_per_cluster * 16;
    if (max_entries > 0x7fffffffu)
        max_entries = 0x7fffffffu;
    for (int idx = 0; (QWORD)idx < max_entries; idx++) {
        int status = fat_read_fte(fs, cur_cluster, idx, &e);
        if (status == FS_ENTRY_END)
            return;
        if (status < 0) {
            lw_puts("DIRECTORY READ FAILED\n\r");
            return;
        }
        if (status == FS_ENTRY_SKIP)
            continue;
        print_field(e.name, 8);
        lw_putc(' ');
        if (e.attr & 0x10)
            lw_puts("<DIR>");
        else
            print_field(e.name + 8, 3);
        lw_putc(' ');
        lw_put_dword(e.length);
        lw_puts(" BYTES AT ");
        lw_put_dword(e.cluster);
        lw_puts("\n\r");
    }
    lw_puts("DIRECTORY CHAIN LIMIT REACHED\n\r");
}

void dir_list(PFS_VOLUME fs, DWORD cluster) {
    lw_puts("LIST FROM CLUSTER ");
    lw_put_dword(cluster);
    lw_puts("\n\r");
    if (!fs || !fs->mounted) {
        return;
    }
    DWORD idx=0;
    DWORD cur=cluster;
    SFT_T res;
    for (DWORD hops = 0; hops < fs->cluster_count; hops++) {
        int entries = 16;
        for (int i = 0; i < entries; i++) {
            int status = fat_read_fte(fs, cur, i, &res);
            if (status == FS_ENTRY_END || status < 0)
                return;
            if (status == FS_ENTRY_SKIP)
                continue;
            print_field(res.name, 8);
            if (res.attr&0x10) {
                lw_puts(" <DIR> ");
            } else {
                lw_puts("  ");
                lw_puts_pad(res.name+8, 3);
                lw_puts("  ");
            }
            lw_put_dword(res.length);
            lw_puts(" AT ");
            lw_put_dword(res.cluster);
            lw_puts("\n\r");
        }
        int status = next_cluster(fs, cur, &cur);
        if (status <= 0)
            return;
    }
}