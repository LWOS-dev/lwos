#include "fs.h"
#include "mem.h"
#include "string.h"

/* 磁盘字段按字节读取, 不依赖指针对齐或 C 结构布局。 */
static WORD read16(PCBYTE p) {
    return (WORD)p[0] | ((WORD)p[1] << 8);
}

static DWORD read32(PCBYTE p) {
    return (DWORD)read16(p) | ((DWORD)read16(p + 2) << 16);
}

static int read_sector(PFS_VOLUME fs, QWORD lba) {
    if (!fs->device || !fs->device->read || lba >= fs->device->sectors)
        return FS_ERR_IO;
    return fs->device->read(fs->device, lba, 1, fs->buffer) ? FS_ERR_IO : 0;
}

static int valid_cluster(PFS_VOLUME fs, DWORD c) {
    return c >= 2 && c - 2 < fs->cluster_count;
}

static QWORD cluster_to_lba(PFS_VOLUME fs, DWORD c) {
    return (QWORD)fs->data_lba + (QWORD)(c - 2) * fs->sectors_per_cluster;
}

/* 1: 有下一簇, 0: 链结束, <0: 错误。 */
int next_cluster(PFS_VOLUME fs, DWORD current, PDWORD next) {
    if (!valid_cluster(fs, current))
        return FS_ERR_FORMAT;
    int err = read_sector(fs, (QWORD)fs->fat_lba + current / 128);
    if (err)
        return err;
    DWORD c = read32(fs->buffer + (current % 128) * 4) & 0x0fffffffu;
    if (c >= 0x0ffffff8u)
        return 0;
    if (!valid_cluster(fs, c) || c >= 0x0ffffff0u)
        return FS_ERR_FORMAT;
    *next = c;
    return 1;
}

int fat_read_fte(PFS_VOLUME fs, DWORD dir_cluster, int index, PSFT_T p) {
    if (!fs || !fs->mounted)
        return FS_ERR_NOT_MOUNTED;
    if (index < 0 || !p)
        return FS_ERR_FORMAT;
    DWORD sector_index = (DWORD)index / 16;
    DWORD hops = sector_index / fs->sectors_per_cluster;
    if (hops >= fs->cluster_count)
        return FS_ERR_FORMAT; /* 限制损坏或循环的簇链。 */
    DWORD c = dir_cluster;
    if (!valid_cluster(fs, c))
        return FS_ERR_FORMAT;
    for (DWORD i = 0; i < hops; i++) {
        int status = next_cluster(fs, c, &c);
        if (status <= 0)
            return status;
    }
    int err = read_sector(fs, cluster_to_lba(fs, c) + sector_index % fs->sectors_per_cluster);
    if (err)
        return err;
    PCBYTE e = fs->buffer + ((DWORD)index % 16) * 32;
    memzero(p, sizeof(*p));
    if (e[0] == 0)
        return FS_ENTRY_END;
    if (e[0] == 0xe5 || (e[11] & 0x0f) == 0x0f || (e[11] & 0x08))
        return FS_ENTRY_SKIP; /* 删除项、长文件名项、卷标。 */
    memcpy(p->name, (PVOID)e, 11);
    if ((BYTE)p->name[0] == 0x05)
        p->name[0] = (CHAR)0xe5;
    p->attr = e[11];
    p->cluster = (((DWORD)read16(e + 0x14) << 16) |
                  read16(e + 0x1a)) & 0x0fffffffu;
    p->length = read32(e + 0x1c);
    return FS_ENTRY_VALID;
}
/* 遍历目录中的全部扇区及 FAT 链, 不依赖目录簇连续。 */
int dir_find(PFS_VOLUME fs, DWORD dir_cluster, PCSTR name,
             PSFT_T out, int is_dir) {
    if (!fs || !fs->mounted)
        return FS_ERR_NOT_MOUNTED;
    if (!name || !out || !valid_cluster(fs, dir_cluster))
        return FS_ERR_FORMAT;
    DWORD current = dir_cluster;
    for (DWORD hops = 0; hops < fs->cluster_count; hops++) {
        int entries = fs->sectors_per_cluster * 16;
        for (int i = 0; i < entries; i++) {
            int status = fat_read_fte(fs, current, i, out);
            if (status == FS_ENTRY_END || status < 0)
                return status;
            if (status == FS_ENTRY_SKIP)
                continue;
            if (strncmp(out->name, name, 11) == 0 &&
                (is_dir < 0 || !!(out->attr & 0x10) == !!is_dir))
                return FS_ENTRY_VALID;
        }
        int status = next_cluster(fs, current, &current);
        if (status <= 0)
            return status;
    }
    return FS_ERR_FORMAT;
}

/* src/length 是一个路径段, out 是空格填充的 8.3 名字。 */
static int make_name83(PCSTR src, int length, PSTR out) {
    int name_length = 0, ext_length = 0, extension = 0;
    if (!length)
        return 0;
    for (int i = 0; i < 11; i++)
        out[i] = ' ';
    for (int i = 0; i < length; i++) {
        BYTE ch = (BYTE)src[i];
        if (ch == '.') {
            if (!name_length || extension)
                return 0;
            extension = 1;
            continue;
        }
        if (ch <= 0x20 || ch >= 0x7f || ch == '"' || ch == '*' ||
            ch == '+' || ch == ',' || ch == ':' || ch == ';' ||
            ch == '<' || ch == '=' || ch == '>' || ch == '?' ||
            ch == '[' || ch == '\\' || ch == ']' || ch == '|')
            return 0;
        if (extension) {
            if (ext_length == 3)
                return 0;
            out[8 + ext_length++] = to_upper(ch);
        } else {
            if (name_length == 8)
                return 0;
            out[name_length++] = to_upper(ch);
        }
    }
    out[11] = 0;
    return name_length && (!extension || ext_length);
}

/* 第一版无当前工作目录, 相对和绝对路径都从卷根目录开始。 */
int fs_lookup(PFS_VOLUME fs, PCSTR path, PSFT_T out) {
    if (!fs || !fs->mounted)
        return FS_ERR_NOT_MOUNTED;
    if (!path || !out)
        return FS_PATH_ERROR;
    PCSTR p = path;
    skip_ws(&p);
    DWORD current = fs->root_cluster;
    while (*p == '/')
        p++;
    if (!*p) {
        memzero(out, sizeof(*out));
        out->attr = 0x10;
        out->cluster = current;
        return 0;
    }
    for (;;) {
        PCSTR end = p;
        /* 最长短名字为 8 + 点 + 3, 提前限制长度。 */
        int length = 0;
        while (*end && *end != '/') {
            if (++length > 12)
                return FS_PATH_ERROR;
            end++;
        }
        CHAR name83[12];
        if (!make_name83(p, length, name83))
            return FS_PATH_ERROR;
        PCSTR next = end;
        while (*next == '/')
            next++;
        int directory = *next || *end == '/';
        SFT_T entry;
        int status = dir_find(fs, current, name83, &entry,
                              directory ? 1 : -1);
        if (status < 0)
            return status;
        if (status == FS_ENTRY_END)
            return FS_PATH_ERROR;
        if (!*next) {
            *out = entry;
            return 0;
        }
        current = entry.cluster;
        p = next;
    }
}
// 只是返回簇号，不构造文件指针
int resolve_path(PFS_VOLUME fs, PCSTR path, PDWORD cluster) {
    if (!cluster)
        return FS_PATH_ERROR;
    SFT_T entry;
    int status = fs_lookup(fs, path, &entry);
    if (status)
        return status;
    *cluster = entry.cluster;
    return 0;
}
// 构造出来这样的一个文件指针
int fs_file_open(PFS_VOLUME fs, PCSTR path, PFS_FILE fp) {
    if (!fp)
        return FS_PATH_ERROR;
    SFT_T entry;
    int status = fs_lookup(fs, path, &entry);
    if (status)
        return status;
    fp->first_cluster = entry.cluster;
    fp->attr = entry.attr;
    fp->length = entry.length;
    fp->current_cluster = 0;
    fp->current_cluster_index = 0;
    fp->opened = 0;
    fp->position = 0;
    fp->volume = fs;
    return 0;
}

int fs_file_read(PFS_FILE file, PVOID buffer, DWORD count, PDWORD read_count) {
    PBYTE dst=buffer;
    DWORD done=0;
}

int fs_mount(PFS_VOLUME fs, PBLKDEV device) {
    if (!fs)
        return FS_ERR_FORMAT;
    memzero(fs, sizeof(*fs));
    fs->device = device;
    fs->mounted = 0;
    if (!fs->device || !fs->device->read || fs->device->sector_size != sizeof(fs->buffer))
        return FS_ERR_IO;
    int err = read_sector(fs, 0);
    if (err)
        return err;
    if (read16(fs->buffer + 0x1fe) != 0xaa55)
        return FS_ERR_FORMAT;

    /* 选 FAT32 分区, 无需依赖 MBR active 标志。 */
    DWORD part_lba = 0, part_sectors = 0;
    for (int i = 0; i < 4; i++) {
        PCBYTE p = fs->buffer + 0x1be + i * 16;
        if (p[4] == 0x0b || p[4] == 0x0c) {
            part_lba = read32(p + 8);
            part_sectors = read32(p + 12);
            break;
        }
    }
    if (!part_lba || !part_sectors || part_lba >= fs->device->sectors ||
        part_sectors > fs->device->sectors - part_lba)
        return FS_ERR_FORMAT;
    err = read_sector(fs, part_lba);
    if (err)
        return err;
    if (read16(fs->buffer + 0x1fe) != 0xaa55 || read16(fs->buffer + 0x0b) != 512)
        return FS_ERR_FORMAT;

    BYTE spc = fs->buffer[0x0d], fats = fs->buffer[0x10];
    WORD reserved = read16(fs->buffer + 0x0e);
    DWORD fat_size = read32(fs->buffer + 0x24);
    DWORD total = read32(fs->buffer + 0x20);
    DWORD root = read32(fs->buffer + 0x2c) & 0x0fffffffu;
    WORD flags = read16(fs->buffer + 0x28);
    BYTE active_fat = (flags & 0x80) ? (flags & 0x0f) : 0;
    QWORD overhead = (QWORD)reserved + (QWORD)fats * fat_size;
    if (!spc || spc > 128 || (spc & (spc - 1)) || !fats || !reserved ||
        !fat_size || !total || total > part_sectors || overhead >= total ||
        active_fat >= fats || read16(fs->buffer + 0x11) || read16(fs->buffer + 0x16) ||
        read16(fs->buffer + 0x2a) || (QWORD)part_lba + total > 0x100000000ull)
        return FS_ERR_FORMAT;
    DWORD clusters = (total - (DWORD)overhead) / spc;
    if (clusters < 65525 || clusters > 0x0fffffeeu || root < 2 ||
        root - 2 >= clusters || (QWORD)fat_size * 128 < (QWORD)clusters + 2)
        return FS_ERR_FORMAT;
    fs->sectors_per_cluster = spc;
    fs->cluster_count = clusters;
    fs->root_cluster = root;
    fs->fat_lba = part_lba + reserved + active_fat * fat_size;
    fs->data_lba = part_lba + (DWORD)overhead;
    fs->mounted = 1;
    return 0;
}
