/* Standalone cat: streaming, exact byte output and failure cleanup. */
#include <assert.h>
#define main cat_main
#define putchar cat_putchar
#define printf cat_printf
#include "../external/coreutils/cat.c"
#undef main
#undef putchar
#undef printf

PVOID *lw_abi_base;
PCLW_ENV lw_env;
static PVOID abi_table[LW_SLOT_COUNT];
static BLKDEV device;
static BYTE contents[1537],output[1537];
static DWORD length,position,used,chunk_limit;
static int mode,mounts,opens,closes,reads,diagnostics;
static PCSTR expected_path="/README.TXT";

int cat_printf(const char *format,...) {
    (void)format;diagnostics++;return 0;
}
int cat_putchar(int ch) {
    if (mode==6 && used==17)return EOF;
    assert(used<sizeof(output));output[used++]=(BYTE)ch;return ch;
}
static PBLKDEV get_device(DWORD index) {
    assert(index==0);return &device;
}
int fs_mount(PFS_VOLUME volume,PBLKDEV dev) {
    assert(dev==&device);mounts++;
    if (mode==1)return FS_ERR_IO;
    volume->mounted=1;return 0;
}
int fs_file_open(PFS_VOLUME volume,PCSTR path,PFS_FILE file) {
    assert(volume->mounted && path);
    for (unsigned i=0;;i++) {
        assert(path[i]==expected_path[i]);if (!path[i])break;
    }
    (void)file;opens++;
    if (mode==2)return FS_ERR_NOT_FOUND;
    if (mode==3)return FS_ERR_IS_DIR;
    return 0;
}
int fs_file_read(PFS_FILE file,PVOID bytes,DWORD capacity,PDWORD count) {
    (void)file;assert(capacity==512);reads++;
    *count=0;
    if (mode==4)return FS_ERR_IO;
    DWORD available=length-position;
    if (available>capacity)available=capacity;
    if (available>chunk_limit)available=chunk_limit;
    for (DWORD i=0;i<available;i++)((PBYTE)bytes)[i]=contents[position+i];
    position+=available;*count=available;
    return mode==5 ? FS_ERR_IO : 0;
}
int fs_file_close(PFS_FILE file) {
    (void)file;closes++;return 0;
}
static void reset(int selected,DWORD size) {
    mode=selected;length=size;position=used=0;chunk_limit=512;
    mounts=opens=closes=reads=diagnostics=0;
}
int main(void) {
    abi_table[LW_SLOT_BLK_GET]=(PVOID)get_device;lw_abi_base=abi_table;
    for (unsigned i=0;i<sizeof(contents);i++)contents[i]=(BYTE)i;
    char *args[]={"cat","/README.TXT",0};
    const DWORD sizes[]={0,1,511,512,513,sizeof(contents)};
    for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        reset(0,sizes[i]);assert(cat_main(2,args)==0);
        assert(used==length && closes==1 && !diagnostics);
        for (DWORD j=0;j<length;j++)assert(output[j]==contents[j]);
    }
    reset(0,sizeof(contents));chunk_limit=7;
    assert(cat_main(2,args)==0 && used==length && reads>200 && closes==1);
    for (DWORD j=0;j<length;j++)assert(output[j]==contents[j]);
    for (int i=1;i<=6;i++) {
        reset(i,sizeof(contents));assert(cat_main(2,args)!=0 && diagnostics==1);
        assert(closes==(i>=4));
        if (i<=4)assert(!used);
        if (i==5)assert(used==512 && reads==1);
        if (i==6)assert(used==17 && reads==1);
    }
    LW_ENV env={.size=sizeof(LW_ENV)};lw_env=&env;
    for (unsigned i=0;i<5;i++)env.cwd[i]="/BIN"[i];
    args[1]="../README.TXT";reset(0,513);
    assert(cat_main(2,args)==0 && used==513 && closes==1);
    env.size=LW_ENV_BASE_SIZE;args[1]="README.TXT";reset(0,1);
    assert(cat_main(2,args)==0 && used==1);
    reset(0,1);assert(cat_main(1,args)==RM_ERR_ARGUMENT && !mounts);
    assert(cat_main(3,args)==RM_ERR_ARGUMENT && !mounts);
    args[1]="";assert(cat_main(2,args)==RM_ERR_ARGUMENT && !mounts);
    return 0;
}
