#ifndef _LW_FS_H
#define _LW_FS_H

#include "dev/blockdev.h"

typedef struct _MBR_T{
    BYTE flag; // 80h -> active, 00h -> inactive
    BYTE chs_beg[3]; // old method
    BYTE series; // partition series
    BYTE chs_end[3];
    DWORD lba;
    DWORD lba_count;
} MBR_T, *PMBR_T; // MBR partition table entry
typedef struct _SFT_T{
    CHAR name[12]; // 8.3 磁盘字段为 11 字节, 另留 NUL
    BYTE attr;
    DWORD cluster;
    DWORD length;
} SFT_T, *PSFT_T; // simplified file table entries

enum {
    FS_ENTRY_END = 0,
    FS_ENTRY_VALID = 1,
    FS_ENTRY_SKIP = 2,
    FS_PATH_ERROR = -1,
    FS_ERR_FORMAT = -2,
    FS_ERR_IO = -4,
    FS_ERR_NOT_MOUNTED = -5
};
/* 调用方持有卷及缓冲区, 不同卷之间不共享解析状态。
 * 同一卷暂不支持并发调用; device 必须在挂载期间一直有效。
 */
typedef struct _FS_VOLUME {
    PBLKDEV device;
    DWORD fat_lba, data_lba, root_cluster, cluster_count;
    BYTE sectors_per_cluster;
    int mounted;
    BYTE buffer[512];
} FS_VOLUME, *PFS_VOLUME;

typedef struct _FS_FILE {
    PBLKDEV device;
    DWORD first_cluster;
    DWORD length;
    DWORD position;
    DWORD current_cluster;
} FS_FILE, *PFS_FILE;

/* 挂载第一个 FAT32 MBR 分区; 不探测设备, 不输出文字。 */
int fs_mount(PFS_VOLUME fs, PBLKDEV device);
/* 完整目录项供将来的文件句柄获取起始簇、长度和属性。 */
int fs_lookup(PFS_VOLUME fs, PCSTR path, PSFT_T out);
int resolve_path(PFS_VOLUME fs, PCSTR path, PDWORD cluster);
/* 返回 END / VALID / SKIP 或负错误码。 */
int fat_read_fte(PFS_VOLUME fs, DWORD dir_cluster, int index, PSFT_T out);
/* is_dir: 0 文件, 1 目录, -1 任意; 返回 VALID / END 或负错误码。 */
int dir_find(PFS_VOLUME fs, DWORD dir_cluster, PCSTR name83,
             PSFT_T out, int is_dir);

#endif
