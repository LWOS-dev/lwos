#include "fs.h"
#include "mem.h"
#include "string.h"
extern int printf(const char *, ...);
static BYTE disk[12][512];
static int fail_lba = -1, reads;
static int rd(PBLKDEV d, QWORD lba, DWORD count, PVOID out) {
    (void)d; reads++;
    if (lba >= 12 || count != 1 || lba == (QWORD)fail_lba)return -1;
    memcpy(out, disk[lba], 512); return 0;
}
static void put32(PBYTE p, DWORD v) {
    for(int i=0;i<4;i++)p[i]=(BYTE)(v>>(8*i));
}
static void entry(int i, PCSTR name, BYTE attr, DWORD c, DWORD length) {
    PBYTE p=disk[2]+i*32;
    memcpy(p,name,11);p[11]=attr;
    p[26]=(BYTE)c;p[27]=(BYTE)(c>>8);put32(p+28,length);
}
#define CHECK(x) do {if (!(x)) {printf("FAIL %d: %s\n",__LINE__,#x);return 1;}} while(0)
int main(void) {
    BLKDEV dev={.sector_size=512,.sectors=12,.read=rd};
    FS_VOLUME fs={.device=&dev,.mounted=1,.sectors_per_cluster=1,
        .fat_lba=1,.data_lba=2,.root_cluster=2,.cluster_count=10};
    put32(disk[1]+2*4,0xfffffff);
    put32(disk[1]+3*4,5);put32(disk[1]+5*4,4);put32(disk[1]+4*4,0xfffffff);
    entry(0,"FILE    BIN",0x20,3,1300);
    entry(1,"EMPTY   TXT",0x20,0,0);
    entry(2,"SUB        ",0x10,6,0);
    entry(3,"BAD     BIN",0x20,0,10);
    for(int i=0;i<512;i++) {disk[3][i]='A';disk[5][i]='B';disk[4][i]='C';}
    FS_FILE f={0}, other={0}; BYTE out[1600]; DWORD n=99, value=99;
    CHECK(fs_file_read(&f,out,1,&n)==FS_ERR_BAD_HANDLE && n==0);
    CHECK(fs_file_tell(&f,&value)==FS_ERR_BAD_HANDLE && value==99);
    CHECK(fs_file_open(&fs,"/",&f)==FS_ERR_IS_DIR && !f.opened);
    CHECK(fs_file_open(&fs,"/SUB",&f)==FS_ERR_IS_DIR && !f.opened);
    CHECK(fs_file_open(&fs,"/MISSING",&f)==FS_ERR_NOT_FOUND);
    CHECK(fs_file_open(&fs,"/SUB/MISSING",&f)==FS_ERR_NOT_FOUND);
    CHECK(fs_file_open(&fs,"/TOOLONGNAME",&f)==FS_PATH_ERROR);
    CHECK(fs_file_open(&fs,"/BAD.BIN",&f)==FS_ERR_FORMAT);
    CHECK(fs_file_open(&fs,"/FILE.BIN",&f)==0 && f.opened);
    CHECK(fs_file_open(&fs,"/EMPTY.TXT",&f)==FS_ERR_BAD_HANDLE && f.length==1300);
    CHECK(fs_file_size(&f,&value)==0 && value==1300);
    CHECK(fs_file_size(&f,0)==FS_ERR_ARGUMENT);
    CHECK(fs_file_tell(&f,0)==FS_ERR_ARGUMENT);
    CHECK(fs_file_read(&f,0,0,&n)==0 && n==0);
    CHECK(fs_file_read(&f,0,1,&n)==FS_ERR_ARGUMENT && n==0);
    CHECK(fs_file_read(&f,out,1,0)==FS_ERR_ARGUMENT && f.position==0);
    CHECK(fs_file_read(&f,out,1600,&n)==0 && n==1300 && f.position==1300);
    for(int i=0;i<1300;i++)CHECK(out[i]==(i<512?'A':i<1024?'B':'C'));
    CHECK(fs_file_eof(&f)==1);
    int before=reads;
    CHECK(fs_file_read(&f,out,10,&n)==0 && n==0 && reads==before);
    CHECK(fs_file_seek(&f,-800,FS_SEEK_END)==0 && f.position==500 && !f.current_cluster);
    CHECK(fs_file_read(&f,out,30,&n)==0 && n==30 && out[11]=='A' && out[12]=='B');
    CHECK(fs_file_seek(&f,-18,FS_SEEK_CUR)==0 && f.position==512);
    CHECK(fs_file_seek(&f,0,FS_SEEK_END)==0 && f.position==1300);
    CHECK(fs_file_seek(&f,1,FS_SEEK_END)==FS_ERR_RANGE && f.position==1300);
    CHECK(fs_file_seek(&f,1301,FS_SEEK_SET)==FS_ERR_RANGE);
    CHECK(fs_file_seek(&f,(-9223372036854775807LL-1),FS_SEEK_CUR)==FS_ERR_RANGE);
    CHECK(fs_file_seek(&f,9223372036854775807LL,FS_SEEK_CUR)==FS_ERR_RANGE);
    CHECK(fs_file_seek(&f,0,99)==FS_ERR_ARGUMENT);
    CHECK(fs_file_seek(&f,0,FS_SEEK_SET)==0);
    fail_lba=5;
    CHECK(fs_file_read(&f,out,900,&n)==FS_ERR_IO && n==512 && f.position==512);
    fail_lba=-1;
    CHECK(fs_file_read(&f,out,388,&n)==0 && n==388 && out[0]=='B' && f.position==900);
    CHECK(fs_file_seek(&f,0,FS_SEEK_SET)==0);
    fail_lba=1;
    CHECK(fs_file_read(&f,out,900,&n)==FS_ERR_IO && n==512 && f.position==512);
    fail_lba=-1;
    CHECK(fs_file_seek(&f,0,FS_SEEK_SET)==0);
    put32(disk[1]+3*4,0xfffffff);
    CHECK(fs_file_read(&f,out,900,&n)==FS_ERR_FORMAT && n==512 && f.position==512);
    put32(disk[1]+3*4,5);
    CHECK(fs_file_open(&fs,"/FILE.BIN",&other)==0);
    CHECK(fs_file_read(&other,out,10,&n)==0 && other.position==10 && f.position==512);
    fs.mounted=0;
    CHECK(fs_file_eof(&f)==FS_ERR_NOT_MOUNTED);
    fs.mounted=1;
    CHECK(fs_file_close(&f)==0 && !f.opened && !f.volume);
    CHECK(fs_file_close(&f)==0);
    CHECK(fs_file_open(&fs,"/EMPTY.TXT",&f)==0);
    before=reads;
    CHECK(fs_file_read(&f,out,1,&n)==0 && !n && reads==before);
    CHECK(fs_file_eof(&f)==1 && fs_file_seek(&f,0,FS_SEEK_END)==0);
    CHECK(fs_file_close(&f)==0);
    printf("PASS: file handles, fragmented reads, EOF, seek bounds, partial I/O and empty files\n");
    return 0;
}
