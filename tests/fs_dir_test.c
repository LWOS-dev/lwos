/* Host test: gcc -fno-builtin -ffunction-sections -fdata-sections
 * -Wl,--gc-sections -Ilib/include tests/fs_dir_test.c lib/src/{fs,mem,string}.c
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
static void full_root(void) {
    for (int i=0;i<16;i++) {
        memcpy(disk[3]+32*i,"USED    TXT",11);
        disk[3][32*i+11]=0x20;
    }
}
int main(void) {
    FS_VOLUME fs; SFT_T e; FEP_T pos, saved;
    fixture(&fs);
    CHECK(dir_create_entry(&fs,2,"NEW     TXT",&e,&pos)==0);
    CHECK(pos.lba==3 && pos.pos==0 && !e.cluster && !e.length);
    CHECK(disk[3][32]==0 && disk[3][16]==0x21);
    saved=pos;
    CHECK(dir_create_entry(&fs,2,"NEW     TXT",&e,&pos)==FS_ERR_EXISTS);
    CHECK(dir_create_entry(&fs,2,"bad     TXT",&e,&pos)==FS_ERR_ARGUMENT);
    disk[3][13]=123; disk[3][64]=0x5a;
    CHECK(dir_update_entry(&fs,&pos,5,700)==0);
    CHECK(get32(disk[3]+28)==700 && disk[3][26]==5);
    CHECK(disk[3][13]==123 && disk[3][64]==0x5a);
    CHECK(fs_lookup_entry(&fs,"/NEW.TXT",&e,&pos)==0 && e.length==700);
    disk[3][11]|=1;
    CHECK(dir_delete_entry(&fs,&pos)==FS_ERR_READ_ONLY);
    disk[3][11]&=~1;
    CHECK(dir_delete_entry(&fs,&pos)==0 && disk[3][0]==0xe5);
    CHECK(dir_create_entry(&fs,2,"NEXT    TXT",&e,&pos)==0 && pos.pos==0);
    CHECK(dir_update_entry(&fs,&pos,0,1)==FS_ERR_ARGUMENT);
    pos.pos=1; CHECK(dir_delete_entry(&fs,&pos)==FS_ERR_RANGE);
    pos=saved; fail_at=writes+1;
    CHECK(dir_update_entry(&fs,&pos,5,100)==FS_ERR_IO);
    CHECK(get32(disk[3]+28)==0);
    fail_at=0; read_fail=1;
    CHECK(dir_delete_entry(&fs,&pos)==FS_ERR_IO);

    fixture(&fs); full_root();
    for (int i=0;i<512;i++)disk[4][i]=0xa5;
    put32(disk[1]+12,0xa0000000); put32(disk[2]+12,0xb0000000);
    CHECK(dir_create_entry(&fs,2,"GROW    TXT",&e,&pos)==0);
    CHECK(pos.lba==4 && pos.pos==0);
    CHECK((get32(disk[1]+8)&0xfffffff)==3 && (get32(disk[2]+8)&0xfffffff)==3);
    CHECK(get32(disk[1]+12)==0xaffffff8 && get32(disk[2]+12)==0xbffffff8);
    CHECK(disk[4][32]==0 && disk[4][511]==0);

    fixture(&fs); full_root(); fat(2,5); fat(5,0xfffffff);
    disk[3][480]=0; disk[6][0]='X'; disk[6][11]=0x20;
    CHECK(dir_create_entry(&fs,2,"LAST    TXT",&e,&pos)==0);
    CHECK(pos.lba==3 && pos.pos==480 && disk[6][0]==0);

    fixture(&fs); full_root();
    for (DWORD c=3;c<10;c++)fat(c,0xfffffff);
    pos.lba=99;pos.pos=77;
    CHECK(dir_find_free(&fs,2,&pos)==FS_ERR_NOFREE);
    CHECK(pos.lba==99 && pos.pos==77);
    fixture(&fs); full_root(); fat(2,2);
    CHECK(dir_find_free(&fs,2,&pos)==FS_ERR_FORMAT);
    fixture(&fs); full_root(); flush_fail=1;
    CHECK(dir_find_free(&fs,2,&pos)==FS_ERR_IO);
    CHECK((get32(disk[1]+8)&0xfffffff)==0xfffffff);
    CHECK((get32(disk[1]+12)&0xfffffff)==0);
    fixture(&fs); fs.fat_mirrored=0; fs.fat_lba=2;
    DWORD v=0xfffffff;
    CHECK(set_fat(&fs,3,&v)==0 && get32(disk[1]+12)==0 && get32(disk[2]+12)==v);
    printf("PASS: directory create/update/delete, growth, terminators, FAT copies and I/O errors\n");
    return 0;
}
