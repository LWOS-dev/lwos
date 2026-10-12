/* Host test: gcc -fno-builtin -ffunction-sections -fdata-sections
 * -Wl,--gc-sections -Ilib/include tests/fs_write_test.c lib/src/{fs,mem,string}.c
 */
#include "fs.h"
#include "mem.h"
#include "string.h"
extern int printf(const char *, ...);
static BYTE disk[20][512];
static int writes, fail_at, read_fail, flush_fail;
static int rd(PBLKDEV d, QWORD lba, DWORD count, PVOID out) {
    (void)d;
    if (read_fail || lba >= 20 || count != 1)return -1;
    memcpy(out, disk[lba], 512); return 0;
}
static int wr(PBLKDEV d, QWORD lba, DWORD count, PCVOID in) {
    (void)d;
    if (++writes == fail_at || lba >= 20 || count != 1)return -1;
    memcpy(disk[lba], in, 512); return 0;
}
static int flush(PBLKDEV d) { (void)d; return flush_fail ? -1 : 0; }
static BLKDEV dev = {.sector_size=512, .sectors=20, .read=rd, .write=wr, .flush=flush};
static void put32(PBYTE p, DWORD v) {
    for (int i=0;i<4;i++)p[i]=(BYTE)(v >> (i*8));
}
static DWORD get32(PCBYTE p) {
    return (DWORD)p[0] | ((DWORD)p[1]<<8) | ((DWORD)p[2]<<16) | ((DWORD)p[3]<<24);
}
static void fat(DWORD c, DWORD v) {
    put32(disk[1]+c*4, v); put32(disk[2]+c*4, v);
}
static void fixture(PFS_VOLUME fs) {
    memzero(disk, sizeof(disk)); memzero(fs, sizeof(*fs));
    writes=fail_at=read_fail=flush_fail=0;
    fs->device=&dev; fs->mounted=1; fs->sectors_per_cluster=1;
    fs->fat_lba=fs->fat_base_lba=1; fs->fat_sectors=1;
    fs->fat_count=2; fs->fat_mirrored=1;
    fs->data_lba=3; fs->root_cluster=2; fs->cluster_count=8;
    fat(2, 0x0fffffff);
}
#define CHECK(x) do {if (!(x)) {printf("FAIL %d: %s\n",__LINE__,#x);return 1;}} while(0)
int main(void) {
    FS_VOLUME fs; FS_FILE f={0}; SFT_T e; FEP_T pos;
    BYTE data[1200], out[1200]; DWORD n;
    for(int i=0;i<1200;i++)data[i]=(BYTE)(i%251);
    fixture(&fs);
    CHECK(dir_create_entry(&fs,2,"WRITE   TXT",&e,&pos)==0);
    CHECK(fs_file_open(&fs,"/WRITE.TXT",&f)==0);
    CHECK(fs_file_write(&f,0,0,&n)==0 && n==0 && !f.first_cluster);
    CHECK(fs_file_write(&f,data,700,&n)==0 && n==700 && f.length==700);
    CHECK(f.first_cluster==3 && get32(disk[1]+12)==4 && get32(disk[2]+12)==4);
    CHECK(disk[5][188]==0 && disk[5][511]==0);
    CHECK(fs_file_seek(&f,0,FS_SEEK_SET)==0);
    CHECK(fs_file_read(&f,out,1200,&n)==0 && n==700);
    for(int i=0;i<700;i++)CHECK(out[i]==data[i]);
    CHECK(fs_file_seek(&f,500,FS_SEEK_SET)==0);
    CHECK(fs_file_write(&f,"abcdefghijklmnopqrst",20,&n)==0 && n==20 && f.length==700);
    CHECK(fs_file_seek(&f,499,FS_SEEK_SET)==0);
    CHECK(fs_file_read(&f,out,22,&n)==0 && out[0]==data[499] && out[1]=='a' && out[20]=='t' && out[21]==data[520]);
    CHECK(fs_file_seek(&f,0,FS_SEEK_END)==0);
    CHECK(fs_file_write(&f,data,500,&n)==0 && f.length==1200 && f.position==1200);
    CHECK(fs_file_close(&f)==0 && fs_file_open(&fs,"/WRITE.TXT",&f)==0 && f.length==1200);
    CHECK(fs_file_seek(&f,700,FS_SEEK_SET)==0);
    CHECK(fs_file_read(&f,out,500,&n)==0 && n==500);
    for(int i=0;i<500;i++)CHECK(out[i]==data[i]);
    CHECK(fs_file_write(&f,data,0xffffffffu,&n)==FS_ERR_RANGE && n==0);
    CHECK(fs_file_write(&f,0,1,&n)==FS_ERR_ARGUMENT);
    CHECK(fs_file_write(&f,data,1,0)==FS_ERR_ARGUMENT);
    disk[3][11]|=1;
    CHECK(fs_file_write(&f,data,1,&n)==FS_ERR_READ_ONLY);
    disk[3][11]&=~1;
    CHECK(fs_file_close(&f)==0);

    /* 只剩一个空闲簇：第一轮可提交，第二轮报告空间不足。 */
    fixture(&fs); CHECK(dir_create_entry(&fs,2,"FULL    TXT",&e,&pos)==0);
    for(DWORD c=4;c<10;c++)fat(c,0xfffffff);
    CHECK(fs_file_open(&fs,"/FULL.TXT",&f)==0);
    CHECK(fs_file_write(&f,data,700,&n)==FS_ERR_NOFREE && n==512 && f.length==512 && f.position==512);
    CHECK(!f.write_failed && get32(disk[3]+28)==512);
    CHECK(fs_file_close(&f)==0);

    /* 数据写失败：未引用的新簇被回收，句柄需关闭。 */
    fixture(&fs); CHECK(dir_create_entry(&fs,2,"FAIL    TXT",&e,&pos)==0);
    CHECK(fs_file_open(&fs,"/FAIL.TXT",&f)==0);
    fail_at=writes+3; /* 两份 FAT 写成功，数据写失败。 */
    CHECK(fs_file_write(&f,data,20,&n)==FS_ERR_IO && !n && !f.length && f.write_failed);
    CHECK(get32(disk[1]+12)==0 && get32(disk[2]+12)==0);
    CHECK(fs_file_read(&f,out,1,&n)==FS_ERR_IO);
    CHECK(fs_file_close(&f)==0);

    /* 目录项写失败：文件首簇仍未知，不释放可能已发布的簇。 */
    fixture(&fs); CHECK(dir_create_entry(&fs,2,"META    TXT",&e,&pos)==0);
    CHECK(fs_file_open(&fs,"/META.TXT",&f)==0);
    fail_at=writes+4;
    CHECK(fs_file_write(&f,data,20,&n)==FS_ERR_IO && !n && !f.length && f.write_failed);
    CHECK(get32(disk[1]+12)!=0 && get32(disk[3]+28)==0);
    CHECK(fs_file_close(&f)==0);

    /* 原有长度以内的断链不可当作正常追加来补。 */
    fixture(&fs); CHECK(dir_create_entry(&fs,2,"SHORT   TXT",&e,&pos)==0);
    fat(3,0xfffffff); CHECK(dir_update_entry(&fs,&pos,3,700)==0);
    CHECK(fs_file_open(&fs,"/SHORT.TXT",&f)==0);
    CHECK(fs_file_seek(&f,512,FS_SEEK_SET)==0);
    CHECK(fs_file_write(&f,data,10,&n)==FS_ERR_FORMAT && n==0);
    CHECK(fs_file_close(&f)==0);

    fixture(&fs); CHECK(dir_create_entry(&fs,2,"SYNC    TXT",&e,&pos)==0);
    CHECK(fs_file_open(&fs,"/SYNC.TXT",&f)==0); flush_fail=1;
    CHECK(fs_file_write(&f,data,10,&n)==FS_ERR_IO && n==0 && f.write_failed);
    CHECK(fs_file_close(&f)==0);
    /* 第二份 FAT 连接失败：已提交的 512 字节保留，不能释放新簇。 */
    fixture(&fs); CHECK(dir_create_entry(&fs,2,"LINK    TXT",&e,&pos)==0);
    CHECK(fs_file_open(&fs,"/LINK.TXT",&f)==0);
    fail_at=writes+9;
    CHECK(fs_file_write(&f,data,700,&n)==FS_ERR_IO && n==512 && f.length==512);
    CHECK(f.write_failed && get32(disk[1]+12)==4 && get32(disk[2]+12)!=4);
    CHECK(get32(disk[1]+16)!=0);
    CHECK(fs_file_close(&f)==0);

    /* 现有碎片链覆盖不分配新簇。 */
    fixture(&fs); CHECK(dir_create_entry(&fs,2,"FRAG    TXT",&e,&pos)==0);
    fat(3,7);fat(7,0xfffffff);CHECK(dir_update_entry(&fs,&pos,3,700)==0);
    CHECK(fs_file_open(&fs,"/FRAG.TXT",&f)==0);
    CHECK(fs_file_seek(&f,500,FS_SEEK_SET)==0);
    CHECK(fs_file_write(&f,data,30,&n)==0 && n==30 && f.length==700);
    CHECK(disk[4][500]==data[0] && disk[8][0]==data[12] && get32(disk[1]+16)==0);
    CHECK(fs_file_close(&f)==0);
    printf("PASS: file write overwrite, append, reopen, disk full, data/metadata/flush failures\n");
    return 0;
}
