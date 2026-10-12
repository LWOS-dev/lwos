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
typedef struct _FEP_T { // lba + pos
    QWORD lba;
    WORD pos;
} FEP_T, *PFEP_T;

enum {
    FS_ENTRY_END = 0,
    FS_ENTRY_VALID = 1,
    FS_ENTRY_SKIP = 2,
    FS_ENTRY_IDLE = 3,
    FS_PATH_ERROR = -1,
    FS_ERR_FORMAT = -2,
    FS_ERR_IO = -4,
    FS_ERR_NOT_MOUNTED = -5,
    FS_ERR_ARGUMENT = -6,
    FS_ERR_NOT_FOUND = -7,
    FS_ERR_IS_DIR = -8,
    FS_ERR_BAD_HANDLE = -9,
    FS_ERR_RANGE = -10,
    FS_ERR_NOFREE = -11,
    FS_ERR_EXISTS = -12,
    FS_ERR_READ_ONLY = -13
};
/* 调用方持有卷及缓冲区, 不同卷之间不共享解析状态。
 * 同一卷暂不支持并发调用; device 必须在挂载期间一直有效。
 */
typedef struct _FS_VOLUME {
    PBLKDEV device;
    DWORD fat_lba, data_lba, root_cluster, cluster_count;
    DWORD fat_base_lba, fat_sectors;
    DWORD fsinfo_lba; /* 0 表示无可用 FSInfo 扇区。 */
    BYTE fat_count, fat_mirrored;
    DWORD cluster_hint; // last idle
    BYTE sectors_per_cluster;
    int mounted;
    BYTE buffer[512];
} FS_VOLUME, *PFS_VOLUME;

/* 文件句柄, 由调用方持有并以 {0} 初始化, 不需要动态分配。
 * 字段由文件接口维护, 调用方不得直接修改或复制已打开的句柄。
 * volume 及其 device 必须存活至关闭; 有打开的文件时不得重新挂载卷。
 * 同一卷的文件操作必须串行执行, 各句柄独立维护读取位置。
 * 写入期间同一文件不能由其他句柄操作，也不能删除/复用其目录项。
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
    FEP_T entry_pos;
    int write_failed; /* 写 I/O 失败后必须关闭、检查卷并重新打开。 */
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
int dir_find(
    PFS_VOLUME fs, 
    DWORD dir_cluster, 
    PCSTR name83,
    PSFT_T out, 
    int is_dir
);

int next_cluster(PFS_VOLUME fs, DWORD current, PDWORD next);

/* 以下为已实现的文件句柄接口（SPC=1）。
 * 除特别注明外, 返回 0 表示成功, 负值表示 FS_* 错误码。
 * 所有输出参数均必填; 失败时保持原值, read/write 的实际字节数除外。
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
int fs_file_read(
    PFS_FILE file, 
    PVOID buffer, 
    DWORD count, 
    PDWORD read_count
);
/* 从 position 覆盖/追加，不自动截断旧尾部。仅支持 SPC=1。
 * 无打开模式参数，文件只读属性或设备无写能力时拒绝写入。
 * 每轮数据及目录项同步成功后提交 position/length/written_count。
 * 失败仍返回已提交字节数；失败轮次可能已修改磁盘，不保证回滚。
 * I/O 错误使句柄失效至关闭重开；close 不负责 flush。
 */
int fs_file_write(
    PFS_FILE file, 
    PCVOID buffer,
    DWORD count, 
    PDWORD written_count
);
/* offset 为有符号字节偏移; 目标位置必须在 [0, length] 内。
 * 失败保持位置不变; 成功使簇缓存失效, 不改变文件长度。
 */
int fs_file_seek(PFS_FILE file, long long offset, int origin);

int fs_file_tell(PFS_FILE file, PDWORD position);
int fs_file_size(PFS_FILE file, PDWORD length);
/* 返回 1 表示 EOF, 0 表示尚未到 EOF, 负值表示错误。 */
int fs_file_eof(PFS_FILE file);

// 无视文件表信息，直接从簇里面读数据，buffer最好对齐
int raw_read(PFS_VOLUME fs, PVOID buffer, DWORD cluster, DWORD sector_count);

int fs_sync(PFS_VOLUME fs);
int get_fat(PFS_VOLUME fs, DWORD cluster, PDWORD value);
int set_fat(PFS_VOLUME fs, DWORD cluster, PCDWORD value);
int find_free_cluser(PFS_VOLUME fs, PDWORD cluster);
int free_clusters(PFS_VOLUME fs, DWORD first_cluster);
int alloc_clusters(
    PFS_VOLUME fs,
    DWORD count,
    PDWORD first_cluster,
    PDWORD last_cluster
);
int append_clusters(PFS_VOLUME fs, DWORD prev_t, DWORD next_h);

int dir_entry_pos(
    PFS_VOLUME fs, 
    DWORD dir_cluster,
    DWORD index, 
    PFEP_T pos
);
int dir_find_entry(
    PFS_VOLUME fs,
    DWORD dir_cluster,
    PCSTR name83, 
    PSFT_T out,
    PFEP_T pos,
    int is_dir
);
int fs_lookup_entry(
    PFS_VOLUME fs, 
    PCSTR path,
    PSFT_T entry, 
    PFEP_T pos
);
int dir_update_entry(
    PFS_VOLUME fs, 
    const FEP_T *pos,
    DWORD first_cluster, 
    DWORD length
);
/* 以下接口仅用于 SPC=1 的短文件名普通文件，同一卷必须串行操作。
 * pos 必须来自该目录的查找结果，不能在删除/复用后继续使用。
 * 返回 0 或负错误码；失败保持输出参数不变，但磁盘修改不保证回滚。
 * 更新不调整簇链；删除不释放簇链，且调用者须保证没有关联的 LFN 项。
 * 写入后由上层 fs_sync；不支持自动生成时间戳。
 */
int dir_delete_entry(PFS_VOLUME fs, const FEP_T *pos);
/* 查找空槽，必要时扩展目录；不预留槽位。 */
int dir_find_free(PFS_VOLUME fs, DWORD dir_cluster, PFEP_T pos);
/* name83 为严格的大写、空格填充的 11 字节短名。
 * 创建首簇/长度均为 0、属性为 ARCHIVE 的空普通文件。
 */
int dir_create_entry(PFS_VOLUME fs, DWORD parent_cluster, PCSTR name83,
                     PSFT_T entry, PFEP_T pos);
/* 创建空文件并递归补齐父目录（8.3，根目录相对路径，不支持 . / ..）。
 * 已有普通文件保持不变；末尾为目录/斜杠则报错。不更新时间戳。
 * 成功的新建操作已同步；失败可能保留已创建的父目录，不保证原子回滚。
 */
int fs_touch(PFS_VOLUME fs, PCSTR path);


#endif
