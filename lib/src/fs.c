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
static void write16(PBYTE p, WORD v) {
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}
static void write32(PBYTE p, DWORD v) {
    write16(p, (WORD)v);
    write16(p + 2, (WORD)(v >> 16));
}

static int read_sector(PFS_VOLUME fs, QWORD lba) {
    if (!fs->device || !fs->device->read || lba >= fs->device->sectors)
        return FS_ERR_IO;
    return fs->device->read(fs->device, lba, 1, fs->buffer) ? FS_ERR_IO : 0;
}
static int write_sector(PFS_VOLUME fs, QWORD lba) {
    if (!fs->device || !fs->device->write || lba >= fs->device->sectors)
        return FS_ERR_IO;
    return fs->device->write(fs->device, lba, 1, fs->buffer) ? FS_ERR_IO : 0;
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
/* pos 可为空; 非空时同时返回最终目录项的磁盘位置。 */
static int lookup_impl(PFS_VOLUME fs, PCSTR path, PSFT_T out,
                       PFEP_T pos, int missing_error) {
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
        if (pos)
            return FS_ERR_ARGUMENT; /* 根目录没有对应的父目录项。 */
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
        FEP_T entry_pos;
        int status = pos
            ? dir_find_entry(fs, current, name83, &entry, &entry_pos,
                             directory ? 1 : -1)
            : dir_find(fs, current, name83, &entry, directory ? 1 : -1);
        if (status < 0)
            return status;
        if (status == FS_ENTRY_END)
            return missing_error;
        if (!*next) {
            *out = entry;
            if (pos)
                *pos = entry_pos;
            return 0;
        }
        current = entry.cluster;
        p = next;
    }
}
static int check_file(PFS_FILE file) {
    if (!file || !file->opened)return FS_ERR_BAD_HANDLE;
    if (file->write_failed)return FS_ERR_IO;
    if (!file->volume || !file->volume->mounted)return FS_ERR_NOT_MOUNTED;
    if (!file->volume->device || !file->volume->device->read)return FS_ERR_IO;
    if (file->volume->sectors_per_cluster != 1)return FS_ERR_FORMAT;
    if (file->position > file->length)return FS_ERR_RANGE;
    return 0;
}
int fs_lookup(PFS_VOLUME fs, PCSTR path, PSFT_T out) {
    return lookup_impl(fs, path, out, 0, FS_PATH_ERROR);
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
/* 所有检查成功后再提交句柄，失败不改变调用方状态。 */
int fs_file_open(PFS_VOLUME fs, PCSTR path, PFS_FILE fp) {
    if (!fp)return FS_ERR_ARGUMENT;
    if (fp->opened)return FS_ERR_BAD_HANDLE;
    if (!path)return FS_ERR_ARGUMENT;
    SFT_T entry;
    int err = lookup_impl(fs, path, &entry, 0, FS_ERR_NOT_FOUND);
    if (err)return err;
    if (entry.attr & 0x10)return FS_ERR_IS_DIR;
    if ((entry.cluster && !valid_cluster(fs, entry.cluster)) ||
        (entry.length && !entry.cluster))return FS_ERR_FORMAT;
    FS_FILE opened = {0};
    opened.volume = fs;
    opened.first_cluster = entry.cluster;
    opened.length = entry.length;
    opened.attr = entry.attr;
    /* 保存目录项位置，供写入提交首簇和长度。 */
    err = lookup_impl(fs, path, &entry, &opened.entry_pos, FS_ERR_NOT_FOUND);
    if (err)return err;
    opened.opened = 1;
    *fp = opened;
    return 0;
}

/* 将簇缓存定位到指定序号；不修改文件的字节位置。 */
static int file_cluster(PFS_FILE file, DWORD target, PDWORD out) {
    PFS_VOLUME fs = file->volume;
    if (target >= fs->cluster_count)return FS_ERR_FORMAT;
    DWORD c = file->first_cluster, index = 0;
    if (file->current_cluster && file->current_cluster_index <= target) {
        c = file->current_cluster;
        index = file->current_cluster_index;
    }
    if (!valid_cluster(fs, c))return FS_ERR_FORMAT;
    while (index < target) {
        int status = next_cluster(fs, c, &c);
        if (status < 0)return status;
        if (!status)return FS_ERR_FORMAT;
        index++;
    }
    file->current_cluster = c;
    file->current_cluster_index = index;
    *out = c;
    return 0;
}

int fs_file_read(PFS_FILE file, PVOID buffer, DWORD count, PDWORD read_count) {
    if (read_count)*read_count = 0;
    int err = check_file(file);
    if (err)return err;
    if (!read_count || (count && !buffer))return FS_ERR_ARGUMENT;
    DWORD remaining = file->length - file->position;
    if (remaining > count)remaining = count;
    PBYTE dst = buffer;
    while (remaining) {
        DWORD c;
        err = file_cluster(file, file->position / 512, &c);
        if (err)return err;
        err = read_sector(file->volume, cluster_to_lba(file->volume, c));
        if (err)return err;
        DWORD offset = file->position % 512;
        DWORD amount = 512 - offset;
        if (amount > remaining)amount = remaining;
        memcpy(dst, file->volume->buffer + offset, amount);
        dst += amount;
        file->position += amount;
        *read_count += amount;
        remaining -= amount;
    }
    return 0;
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
    if (spc != 1 || !fats || !reserved ||
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
    fs->fat_base_lba = part_lba + reserved;
    fs->fat_sectors = fat_size;
    fs->fat_count = fats;
    fs->fat_mirrored = !(flags & 0x80);
    WORD info = read16(fs->buffer + 0x30);
    fs->fsinfo_lba = (info && info < reserved) ? part_lba + info : 0;
    fs->data_lba = part_lba + (DWORD)overhead;
    fs->mounted = 1;
    return 0;
}
int fs_file_seek(PFS_FILE file, long long offset, int origin) {
    int err = check_file(file);
    if (err)return err;
    long long base;
    switch (origin) {
        case FS_SEEK_SET: base = 0; break;
        case FS_SEEK_CUR: base = file->position; break;
        case FS_SEEK_END: base = file->length; break;
        default: return FS_ERR_ARGUMENT;
    }
    /* 先比较再加法，避免 LLONG_MIN/LLONG_MAX 偏移造成溢出。 */
    if (offset < -base || offset > (long long)file->length - base)
        return FS_ERR_RANGE;
    file->position = (DWORD)(base + offset);
    file->current_cluster = 0;
    file->current_cluster_index = 0;
    return 0;
}
int fs_file_tell(PFS_FILE file, PDWORD position) {
    int err = check_file(file);
    if (err)return err;
    if (!position)return FS_ERR_ARGUMENT;
    *position = file->position;
    return 0;
}
int fs_file_size(PFS_FILE file, PDWORD length) {
    int err = check_file(file);
    if (err)return err;
    if (!length)return FS_ERR_ARGUMENT;
    *length = file->length;
    return 0;
}
int fs_file_eof(PFS_FILE file) {
    int err = check_file(file);
    if (err)return err;
    return file->position == file->length;
}
int fs_file_close(PFS_FILE file) {
    if (!file)return FS_ERR_BAD_HANDLE;
    memzero(file, sizeof(*file));
    return 0;
}
int raw_read(PFS_VOLUME fs, PVOID buffer, DWORD cluster, DWORD sector_count) {
    PBYTE p=buffer;
    DWORD c=cluster;
    for (int i=0; i<sector_count; i++) {
        int s=read_sector(fs, cluster_to_lba(fs, c));
        if (s!=0) {
            return s;
        }
        memcpy(p, fs->buffer, 512);
        p+=512;
        next_cluster(fs, c, &c);
    }
    return 0;
}
int fs_sync(PFS_VOLUME fs) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!fs->device || !fs->device->flush)return FS_ERR_IO;
    return fs->device->flush(fs->device) ? FS_ERR_IO : 0;
}

int get_fat(PFS_VOLUME fs, DWORD cluster, PDWORD value) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!fs->device || !fs->device->read)return FS_ERR_IO;
    if (!value)return FS_ERR_ARGUMENT;
    //if (!valid_cluster(fs,cluster))return FS_ERR_RANGE;
    QWORD lba=(QWORD)fs->fat_lba+(cluster>>7);
    DWORD offset=(cluster&0x7f)<<2;
    int err=read_sector(fs,lba);
    if (err)return err;
    *value=read32(fs->buffer+offset)&0xfffffff;
    return 0;
}
int set_fat(PFS_VOLUME fs, DWORD cluster, PCDWORD value) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!fs->device || !fs->device->write)return FS_ERR_IO;
    if (!value)return FS_ERR_ARGUMENT;
    if (!valid_cluster(fs,cluster))return FS_ERR_RANGE;
    DWORD offset=(cluster&0x7f)<<2;
    DWORD replacement = *value & 0x0fffffff;
    DWORD copies = fs->fat_mirrored ? fs->fat_count : 1;
    for (DWORD i = 0; i < copies; i++) {
        QWORD base = fs->fat_mirrored
            ? (QWORD)fs->fat_base_lba + (QWORD)i * fs->fat_sectors
            : fs->fat_lba;
        QWORD lba = base + (cluster >> 7);
        int err = read_sector(fs, lba);
        if (err)return err;
        DWORD tmp = (read32(fs->buffer + offset) & 0xf0000000) | replacement;
        write32(fs->buffer + offset, tmp);
        err = write_sector(fs, lba);
        if (err)return err;
    }
    return 0;
}
int find_free_cluser(PFS_VOLUME fs, PDWORD cluster) {
    if (!fs||!fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!fs->device || !fs->device->flush)return FS_ERR_IO;
    if (!cluster)return FS_ERR_ARGUMENT;
    for (DWORD c=2; (c-2) < fs->cluster_count; c++) {
        DWORD value;
        int err=get_fat(fs, c, &value);
        if (err)return err;
        if (value == 0) {
            *cluster=c;
            return 0;
        }
    }
    return FS_ERR_NOFREE;
}
static int clear_cluster(PFS_VOLUME fs, DWORD cluster) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!valid_cluster(fs,cluster))return FS_ERR_RANGE;
    QWORD lba=cluster_to_lba(fs,cluster);
    memzero(fs->buffer,fs->device->sector_size);
    int err=write_sector(fs,lba);
    if (err)return err;
    return 0;
}
int free_clusters(PFS_VOLUME fs, DWORD first_cluster) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!fs->device)return FS_ERR_IO;
    DWORD c=first_cluster;
    DWORD n=0;
    const DWORD v=0; // zero
    while (1) {
        int status=next_cluster(fs,c,&n);
        if (status<0)return status;
        int err=set_fat(fs,c,&v);
        if (err)return err;
        if (status==0)return 0;
        c=n;
    }
    return 0;
}
int alloc_clusters(
    PFS_VOLUME fs,
    DWORD count,
    PDWORD first_cluster,
    PDWORD last_cluster
) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!fs->device)return FS_ERR_IO;
    if (!first_cluster || !last_cluster)return FS_ERR_ARGUMENT;
    if (first_cluster==last_cluster)return FS_ERR_ARGUMENT;
    if (count<=0)return FS_ERR_ARGUMENT;
    DWORD h=0,t=0;
    DWORD eoc=0xffffff8;
    for (int i=0; i<count; i++) {
        DWORD c=0;
        int status=find_free_cluser(fs,&c);
        if (status!=0) {
            if (h) {
                int err=free_clusters(fs,h);
                if (err)return err;
            }
            return status;
        }
        int err=set_fat(fs,c,&eoc);
        if (err)return err;
        if (h==0) {
            h=c;
        } else {
            err=set_fat(fs,t,&c);
            if (err)return err;
        }
        t=c;
    }
    *first_cluster=h;
    *last_cluster=t;
    return 0;
}
int append_clusters(PFS_VOLUME fs, DWORD prev_t, DWORD next_h) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!valid_cluster(fs, prev_t) ||
        !valid_cluster(fs, next_h) ||
        prev_t == next_h)
        return FS_ERR_ARGUMENT;
    DWORD v;
    int err=get_fat(fs,prev_t,&v);
    if (err!=0)return err;
    if (v<0xffffff8)return FS_ERR_FORMAT;
    return set_fat(fs,prev_t,&next_h);
}

int dir_entry_pos(
    PFS_VOLUME fs, 
    DWORD dir_cluster,
    DWORD index, 
    PFEP_T pos
) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!pos)return FS_ERR_ARGUMENT;
    if (!valid_cluster(fs,dir_cluster))return FS_ERR_RANGE;
    DWORD hops=index/16;
    if (hops>=fs->cluster_count)return FS_ERR_RANGE;
    DWORD c=dir_cluster;
    for (int i=0; i<hops; i++) {
        int status=next_cluster(fs,c,&c);
        if (status<=0)return status;
    }
    pos->lba=cluster_to_lba(fs,c);
    pos->pos=(index%16)*32;
    return 1;
}
int dir_find_entry(
    PFS_VOLUME fs,
    DWORD dir_cluster,
    PCSTR name83, 
    PSFT_T out,
    PFEP_T pos,
    int is_dir
) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (!name83 || !out || !pos)return FS_ERR_ARGUMENT;
    if (!valid_cluster(fs,dir_cluster))return FS_ERR_FORMAT;
    if (is_dir<-1 || is_dir>1)return FS_ERR_RANGE;
    DWORD c=dir_cluster;
    for (DWORD hops = 0; hops < fs->cluster_count; hops++) {
        for (int i = 0; i < 16; i++) {
            SFT_T entry;
            int status = fat_read_fte(fs,c,i,&entry);
            if (status<0 || status==FS_ENTRY_END)return status;
            if (status!=FS_ENTRY_VALID)continue;
            if (strncmp(entry.name,name83,11)!=0)continue;
            if (is_dir>=0 &&
                !!(entry.attr&0x10)!=is_dir)continue;
            pos->lba=cluster_to_lba(fs,c);
            pos->pos = (WORD)(i * 32);
            *out=entry;
            return FS_ENTRY_VALID;
        }
        int status=next_cluster(fs,c,&c);
        if (status<=0)return status;
    }
    return FS_ERR_FORMAT;
}
int fs_lookup_entry(
    PFS_VOLUME fs, 
    PCSTR path,
    PSFT_T entry, 
    PFEP_T pos
) {
    if (!pos)
        return FS_ERR_ARGUMENT;
    return lookup_impl(fs, path, entry, pos, FS_PATH_ERROR);
}

/* 目录修改只接受由本卷查找得到的槽位；范围检查不能证明槽位归属。 */
static int dir_write_ready(PFS_VOLUME fs) {
    if (!fs || !fs->mounted)return FS_ERR_NOT_MOUNTED;
    if (fs->sectors_per_cluster != 1)return FS_ERR_FORMAT;
    if (!fs->device || !fs->device->read || !fs->device->write)
        return FS_ERR_IO;
    return 0;
}

static int dir_read_slot(PFS_VOLUME fs, const FEP_T *pos) {
    int err = dir_write_ready(fs);
    if (err)return err;
    if (!pos)return FS_ERR_ARGUMENT;
    if (pos->pos > 480 || pos->pos % 32 || pos->lba < fs->data_lba ||
        pos->lba - fs->data_lba >= fs->cluster_count)
        return FS_ERR_RANGE;
    return read_sector(fs, pos->lba);
}

static int dir_regular_slot(PCBYTE e) {
    if (!e[0] || e[0] == 0xe5)return FS_ERR_NOT_FOUND;
    if ((e[11] & 0x0f) == 0x0f || (e[11] & 0x08))return FS_ERR_ARGUMENT;
    if (e[11] & 0x10)return FS_ERR_IS_DIR;
    if (e[11] & 0x01)return FS_ERR_READ_ONLY;
    return 0;
}

int dir_update_entry(PFS_VOLUME fs, const FEP_T *pos,
                     DWORD first_cluster, DWORD length) {
    int err = dir_read_slot(fs, pos);
    if (err)return err;
    if ((first_cluster && !valid_cluster(fs, first_cluster)) ||
        (!first_cluster && length))return FS_ERR_ARGUMENT;
    PBYTE e = fs->buffer + pos->pos;
    err = dir_regular_slot(e);
    if (err)return err;
    write16(e + 0x14, (WORD)(first_cluster >> 16));
    write16(e + 0x1a, (WORD)first_cluster);
    write32(e + 0x1c, length);
    e[11] |= 0x20; /* ARCHIVE */
    return write_sector(fs, pos->lba);
}

int dir_delete_entry(PFS_VOLUME fs, const FEP_T *pos) {
    int err = dir_read_slot(fs, pos);
    if (err)return err;
    PBYTE e = fs->buffer + pos->pos;
    err = dir_regular_slot(e);
    if (err)return err;
    e[0] = 0xe5;
    return write_sector(fs, pos->lba);
}

/* 分配前使磁盘上的空闲计数失效，避免留下错误的已知计数。 */
static int invalidate_fsinfo(PFS_VOLUME fs) {
    int err;
    if (!fs->fsinfo_lba)return 0;
    err = read_sector(fs, fs->fsinfo_lba);
    if (err)return err;
    if (read32(fs->buffer) != 0x41615252 ||
        read32(fs->buffer + 484) != 0x61417272 ||
        read32(fs->buffer + 508) != 0xaa550000)return 0;
    write32(fs->buffer + 488, 0xffffffffu);
    write32(fs->buffer + 492, 0xffffffffu);
    err = write_sector(fs, fs->fsinfo_lba);
    return err ? err : fs_sync(fs);
}

int dir_find_free(PFS_VOLUME fs, DWORD dir_cluster, PFEP_T pos) {
    int err = dir_write_ready(fs);
    if (err)return err;
    if (!pos)return FS_ERR_ARGUMENT;
    if (!valid_cluster(fs, dir_cluster))return FS_ERR_RANGE;
    DWORD c = dir_cluster;
    for (DWORD hops = 0; hops < fs->cluster_count; hops++) {
        QWORD lba = cluster_to_lba(fs, c);
        err = read_sector(fs, lba);
        if (err)return err;
        for (WORD offset = 0; offset < 512; offset += 32) {
            if (!fs->buffer[offset] || fs->buffer[offset] == 0xe5) {
                pos->lba = lba;
                pos->pos = offset;
                return 0;
            }
        }
        DWORD next;
        int status = next_cluster(fs, c, &next);
        if (status < 0)return status;
        if (status == 1) {
            c = next;
            continue;
        }
        /* 先初始化新簇并同步，再让旧目录链引用它。 */
        err = invalidate_fsinfo(fs);
        if (err)return err;
        DWORD first, last;
        err = alloc_clusters(fs, 1, &first, &last);
        if (err)return err;
        err = clear_cluster(fs, first);
        if (!err)err = fs_sync(fs);
        if (err) {
            int cleanup = free_clusters(fs, first);
            return cleanup ? cleanup : err;
        }
        err = append_clusters(fs, c, first);
        if (err)return err; /* 连接可能部分成功，不能盲目释放。 */
        pos->lba = cluster_to_lba(fs, first);
        pos->pos = 0;
        return 0;
    }
    return FS_ERR_FORMAT;
}

/* 将严格的磁盘短名还原再编码，用既有规则校验，拒绝小写、点号和填充错误。 */
static int valid_name83(PCSTR name) {
    CHAR text[13], normalized[12];
    int base = 8, ext = 3, n = 0;
    while (base && name[base - 1] == ' ')base--;
    while (ext && name[8 + ext - 1] == ' ')ext--;
    if (!base)return 0;
    for (int i = 0; i < base; i++)text[n++] = name[i];
    if (ext) {
        text[n++] = '.';
        for (int i = 0; i < ext; i++)text[n++] = name[8 + i];
    }
    if (!make_name83(text, n, normalized))return 0;
    for (int i = 0; i < 11; i++)
        if (name[i] != normalized[i])return 0;
    return 1;
}

static int create_entry(PFS_VOLUME fs, DWORD parent_cluster, PCSTR name83,
                        BYTE attr, DWORD first, PSFT_T entry, PFEP_T pos) {
    int err = dir_write_ready(fs);
    if (err)return err;
    if (!name83 || !entry || !pos || !valid_name83(name83))
        return FS_ERR_ARGUMENT;
    /* 复制输入，避免后续读取覆盖调用方传入的卷缓冲区内容。 */
    SFT_T created;
    memzero(&created, sizeof(created));
    memcpy(created.name, name83, 11);
    created.attr = attr;
    created.cluster = first;
    SFT_T existing;
    FEP_T slot;
    int status = dir_find_entry(fs, parent_cluster, created.name,
                                &existing, &slot, -1);
    if (status < 0)return status;
    if (status == FS_ENTRY_VALID)return FS_ERR_EXISTS;
    err = dir_find_free(fs, parent_cluster, &slot);
    if (err)return err;
    err = dir_read_slot(fs, &slot);
    if (err)return err;
    if (!fs->buffer[slot.pos]) {
        if (slot.pos < 480) {
            fs->buffer[slot.pos + 32] = 0;
        } else {
            /* 结束标记跨簇时，先提交后继标记，再覆盖原结束项。 */
            DWORD c = (DWORD)(slot.lba - fs->data_lba) + 2, next;
            status = next_cluster(fs, c, &next);
            if (status < 0)return status;
            if (status == 1) {
                QWORD lba = cluster_to_lba(fs, next);
                err = read_sector(fs, lba);
                if (err)return err;
                fs->buffer[0] = 0;
                err = write_sector(fs, lba);
                if (!err)err = fs_sync(fs);
                if (err)return err;
            }
            err = dir_read_slot(fs, &slot);
            if (err)return err;
        }
    }
    PBYTE e = fs->buffer + slot.pos;
    memzero(e, 32);
    memcpy(e, created.name, 11);
    e[11] = created.attr;
    write16(e + 20, (WORD)(first >> 16));
    write16(e + 26, (WORD)first);
    /* 无 RTC 时使用 FAT 日期起点 1980-01-01。 */
    write16(e + 16, 0x21);
    write16(e + 18, 0x21);
    write16(e + 24, 0x21);
    err = write_sector(fs, slot.lba);
    if (err)return err;
    *entry = created;
    *pos = slot;
    return 0;
}

int dir_create_entry(PFS_VOLUME fs, DWORD parent_cluster, PCSTR name83,
                     PSFT_T entry, PFEP_T pos) {
    return create_entry(fs, parent_cluster, name83, 0x20, 0, entry, pos);
}

/* 调用者已检查重名；目录内容初始化落盘后才发布父目录项。 */
static int create_directory(PFS_VOLUME fs, DWORD parent, PCSTR name83,
                            PSFT_T out) {
    int err = invalidate_fsinfo(fs);
    if (err)return err;
    DWORD first, last;
    err = alloc_clusters(fs, 1, &first, &last);
    if (err)return err;
    memzero(fs->buffer, 512);
    for (int i = 0; i < 2; i++) {
        PBYTE e = fs->buffer + i * 32;
        memcpy(e, i ? "..         " : ".          ", 11);
        e[11] = 0x10;
        DWORD c = i ? (parent == fs->root_cluster ? 0 : parent) : first;
        write16(e + 20, (WORD)(c >> 16));
        write16(e + 26, (WORD)c);
        write16(e + 16, 0x21);
        write16(e + 18, 0x21);
        write16(e + 24, 0x21);
    }
    err = write_sector(fs, cluster_to_lba(fs, first));
    if (!err)err = fs_sync(fs);
    if (err) {
        int cleanup = free_clusters(fs, first);
        return cleanup ? cleanup : err;
    }
    FEP_T pos;
    err = create_entry(fs, parent, name83, 0x10, first, out, &pos);
    if (err) {
        /* I/O 错误可能已发布目录项；此时不能释放其簇。 */
        if (err != FS_ERR_IO) {
            int cleanup = free_clusters(fs, first);
            if (cleanup)return cleanup;
        }
        return err;
    }
    return fs_sync(fs);
}

int fs_touch(PFS_VOLUME fs, PCSTR path) {
    int err = dir_write_ready(fs);
    if (err)return err;
    if (!path)return FS_ERR_ARGUMENT;
    PCSTR start = path;
    skip_ws(&start);
    while (*start == '/')start++;
    if (!*start)return FS_PATH_ERROR;

    /* 先校验整条路径，避免因后面的非法名字创建半截目录。 */
    PCSTR p = start;
    CHAR name83[12];
    for (;;) {
        int length = 0;
        while (p[length] && p[length] != '/') {
            if (++length > 12)return FS_PATH_ERROR;
        }
        if (!make_name83(p, length, name83))return FS_PATH_ERROR;
        p += length;
        if (!*p)break;
        while (*p == '/')p++;
        if (!*p)return FS_PATH_ERROR; /* 必须以文件名结束。 */
    }

    DWORD parent = fs->root_cluster;
    p = start;
    for (;;) {
        int length = 0;
        while (p[length] && p[length] != '/')length++;
        make_name83(p, length, name83);
        p += length;
        while (*p == '/')p++;
        SFT_T entry;
        FEP_T pos;
        int status = dir_find_entry(fs, parent, name83, &entry, &pos, -1);
        if (status < 0)return status;
        if (!*p) {
            if (status == FS_ENTRY_VALID)
                return (entry.attr & 0x10) ? FS_ERR_IS_DIR : 0;
            err = dir_create_entry(fs, parent, name83, &entry, &pos);
            return err ? err : fs_sync(fs);
        }
        if (status == FS_ENTRY_VALID) {
            if (!(entry.attr & 0x10))return FS_PATH_ERROR;
            if (!valid_cluster(fs, entry.cluster))return FS_ERR_FORMAT;
        } else {
            err = create_directory(fs, parent, name83, &entry);
            if (err)return err;
        }
        parent = entry.cluster;
    }
}

/* I/O 失败后禁止继续使用旧缓存；关闭、检查卷并重新打开。 */
static int file_write_error(PFS_FILE file, int err) {
    if (err == FS_ERR_IO)file->write_failed = 1;
    return err;
}

int fs_file_write(PFS_FILE file, PCVOID buffer, DWORD count,
                  PDWORD written_count) {
    if (written_count)*written_count = 0;
    int err = check_file(file);
    if (err)return err;
    if (!written_count || (count && !buffer))return FS_ERR_ARGUMENT;
    PFS_VOLUME fs = file->volume;
    if (file->attr & 1)return FS_ERR_READ_ONLY;
    if (!fs->device->write || !fs->device->flush)return FS_ERR_IO;
    if (count > 0xffffffffu - file->position)return FS_ERR_RANGE;
    if (!count)return 0;

    /* 不允许通过过时的句柄覆盖已被另一个操作更改的长度/首簇。 */
    err = dir_read_slot(fs, &file->entry_pos);
    if (err)return file_write_error(file, err);
    PCBYTE e = fs->buffer + file->entry_pos.pos;
    err = dir_regular_slot(e);
    if (err)return err;
    DWORD disk_first = (((DWORD)read16(e + 20) << 16) | read16(e + 26)) & 0x0fffffff;
    if (disk_first != file->first_cluster || read32(e + 28) != file->length)
        return FS_ERR_BAD_HANDLE;

    PCBYTE src = buffer;
    while (*written_count < count) {
        DWORD index = file->position / 512, offset = file->position % 512;
        DWORD c = 0, tail = 0, fresh = 0;
        DWORD used = file->length / 512 + (file->length % 512 != 0);
        if (index >= fs->cluster_count)return FS_ERR_NOFREE;
        if (!file->first_cluster) {
            if (file->length || file->position)return FS_ERR_FORMAT;
        } else if (index < used || !index) {
            err = file_cluster(file, index, &c);
            if (err)return file_write_error(file, err);
        } else {
            /* 只在文件长度之外扩展；原长度以内的短链属于损坏。 */
            err = file_cluster(file, index - 1, &tail);
            if (err)return file_write_error(file, err);
            int status = next_cluster(fs, tail, &c);
            if (status < 0)return file_write_error(file, status);
            if (!status)c = 0;
        }
        if (!c) {
            err = invalidate_fsinfo(fs);
            if (err)return file_write_error(file, err);
            DWORD last;
            err = alloc_clusters(fs, 1, &c, &last);
            if (err)return file_write_error(file, err);
            fresh = 1;
        }
        DWORD amount = 512 - offset;
        if (amount > count - *written_count)amount = count - *written_count;
        if (fresh)memzero(fs->buffer, 512);
        else if (offset || amount != 512) {
            err = read_sector(fs, cluster_to_lba(fs, c));
            if (err)return file_write_error(file, err);
        }
        memcpy(fs->buffer + offset, src, amount);
        err = write_sector(fs, cluster_to_lba(fs, c));
        if (!err)err = fs_sync(fs); /* 数据先于新增引用/长度落盘。 */
        if (err) {
            if (fresh) {
                int cleanup = free_clusters(fs, c); /* 尚未被文件引用。 */
                if (cleanup)err = cleanup;
            }
            return file_write_error(file, err);
        }
        if (fresh && tail) {
            err = append_clusters(fs, tail, c);
            if (!err)err = fs_sync(fs);
            if (err)return file_write_error(file, err); /* 不能盲目释放。 */
        }
        DWORD first = file->first_cluster ? file->first_cluster : c;
        DWORD position = file->position + amount;
        DWORD length = position > file->length ? position : file->length;
        /* 即使长度未变，也更新 ARCHIVE 属性。 */
        err = dir_update_entry(fs, &file->entry_pos, first, length);
        if (!err)err = fs_sync(fs);
        if (err)return file_write_error(file, err);
        file->first_cluster = first;
        file->length = length;
        file->position = position;
        file->attr |= 0x20;
        file->current_cluster = c;
        file->current_cluster_index = index;
        *written_count += amount;
        src += amount;
    }
    return 0;
}
