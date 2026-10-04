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
    FS_ERR_NOT_MOUNTED = -5,
    FS_ERR_ARGUMENT = -6,
    FS_ERR_NOT_FOUND = -7,
    FS_ERR_IS_DIR = -8,
    FS_ERR_BAD_HANDLE = -9,
    FS_ERR_RANGE = -10
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

/* 只读文件句柄, 由调用方持有并以 {0} 初始化, 不需要动态分配。
 * 字段由文件接口维护, 调用方不得直接修改或复制已打开的句柄。
 * volume 及其 device 必须存活至关闭; 有打开的文件时不得重新挂载卷。
 * 同一卷的文件操作必须串行执行, 各句柄独立维护读取位置。
 */
typedef struct _FS_FILE {
    PFS_VOLUME volume;
    DWORD first_cluster;
    DWORD length;
    DWORD position;
    DWORD current_cluster;       /* 缓存簇; 0 表示无有效缓存 */
    DWORD current_cluster_index; /* 缓存簇在文件簇链中的索引, 从 0 开始 */
    BYTE attr;
    int opened;
} FS_FILE, *PFS_FILE;

enum {
    FS_SEEK_SET = 0,
    FS_SEEK_CUR = 1,
    FS_SEEK_END = 2
};

/* 挂载第一个 FAT32 MBR 分区; 不探测设备, 不输出文字。 */
int fs_mount(PFS_VOLUME fs, PBLKDEV device);
/* 完整目录项包含起始簇、长度和属性。 */
int fs_lookup(PFS_VOLUME fs, PCSTR path, PSFT_T out);
int resolve_path(PFS_VOLUME fs, PCSTR path, PDWORD cluster);
/* 返回 END / VALID / SKIP 或负错误码。 */
int fat_read_fte(PFS_VOLUME fs, DWORD dir_cluster, int index, PSFT_T out);
/* is_dir: 0 文件, 1 目录, -1 任意; 返回 VALID / END 或负错误码。 */
int dir_find(PFS_VOLUME fs, DWORD dir_cluster, PCSTR name83,
             PSFT_T out, int is_dir);

int next_cluster(PFS_VOLUME fs, DWORD current, PDWORD next);

/* 以下为文件句柄接口声明, 暂无实现。
 * 除特别注明外, 返回 0 表示成功, 负值表示 FS_* 错误码。
 * 所有输出参数均必填; 失败时保持原值, fs_file_read 的 read_count 除外。
 */

/* 按 fs_lookup 的路径规则打开普通文件 (当前仅支持 8.3 短文件名)。
 * file 必须处于关闭状态; 失败保持关闭, 重复打开返回 BAD_HANDLE。
 * 路径不存在返回 NOT_FOUND, 目录返回 IS_DIR; 空文件允许打开。
 */
int fs_file_open(PFS_VOLUME fs, PCSTR path, PFS_FILE file);

/* 清空句柄, 不释放调用方内存; 对已关闭的句柄重复关闭仍返回成功。 */
int fs_file_close(PFS_FILE file);

/* 从当前位置读取最多 count 字节, 通过 read_count 返回实际读取量。
 * 到达 EOF 返回成功且读取量为 0; count 为 0 时 buffer 可以为空。
 * 即使中途发生错误也返回已读取量, position 按该读取量前进。
 * 参数或句柄校验失败时, 若 read_count 非空则将其置 0。
 */
int fs_file_read(PFS_FILE file, PVOID buffer, DWORD count, PDWORD read_count);

/* offset 为有符号字节偏移; 目标位置必须在 [0, length] 内。
 * 失败保持位置不变; 成功使簇缓存失效, 不改变文件长度。
 */
int fs_file_seek(PFS_FILE file, long long offset, int origin);

int fs_file_tell(PFS_FILE file, PDWORD position);
int fs_file_size(PFS_FILE file, PDWORD length);
/* 返回 1 表示 EOF, 0 表示尚未到 EOF, 负值表示错误。 */
int fs_file_eof(PFS_FILE file);

#endif
