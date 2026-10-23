/* Streaming hex dump: formatting, short reads, ranges and failure cleanup. */
#include <assert.h>
#define main xxd_main
#define printf xxd_printf
#include "../external/coreutils/xxd.c"
#undef main
#undef printf

PVOID *lw_abi_base;
PCLW_ENV lw_env;
static PVOID abi_table[LW_SLOT_COUNT];
static BLKDEV device;
static BYTE contents[1537];
static char output[12000];
static DWORD length,position,used,chunk_limit;
static int mode,mounts,closes,reads;
static PCSTR expected_path="/DATA.BIN";
static int same(PCSTR a,PCSTR b) {
    while (*a && *a==*b) {a++;b++;}
    return *a==*b;
}
int xxd_printf(const char *format,...) {
    if (mode==6)return EOF;
    va_list args;va_start(args,format);
    int n=vsnprintf(output+used,sizeof(output)-used,format,args);
    va_end(args);
    assert(n>=0 && (unsigned)n<sizeof(output)-used);used+=n;return n;
}
static PBLKDEV get_device(DWORD index) {assert(!index);return &device;}
int fs_mount(PFS_VOLUME volume,PBLKDEV dev) {
    assert(dev==&device);mounts++;
    if (mode==1)return FS_ERR_IO;
    volume->mounted=1;return 0;
}
int fs_file_open(PFS_VOLUME volume,PCSTR path,PFS_FILE file) {
    (void)file;assert(volume->mounted && same(path,expected_path));
    return mode==2 ? FS_ERR_NOT_FOUND : 0;
}
int fs_file_size(PFS_FILE file,PDWORD size) {(void)file;*size=length;return 0;}
int fs_file_seek(PFS_FILE file,long long offset,int origin) {
    (void)file;assert(origin==FS_SEEK_SET && offset>=0 && offset<=length);
    if (mode==3)return FS_ERR_IO;
    position=offset;return 0;
}
int fs_file_read(PFS_FILE file,PVOID buffer,DWORD capacity,PDWORD count) {
    (void)file;reads++;*count=0;
    if (mode==4)return FS_ERR_IO;
    DWORD n=length-position;
    if (n>capacity)n=capacity;
    if (n>chunk_limit)n=chunk_limit;
    for (DWORD i=0;i<n;i++)((PBYTE)buffer)[i]=contents[position+i];
    position+=n;*count=n;return mode==5 ? FS_ERR_IO : 0;
}
int fs_file_close(PFS_FILE file) {(void)file;closes++;return 0;}
static void reset(int selected,DWORD size) {
    mode=selected;length=size;position=used=0;chunk_limit=64;
    mounts=closes=reads=0;output[0]=0;
}
int main(void) {
    abi_table[LW_SLOT_BLK_GET]=(PVOID)get_device;lw_abi_base=abi_table;
    for (unsigned i=0;i<sizeof(contents);i++)contents[i]=(BYTE)('A'+i%26);
    char *args[]={"xxd","DATA.BIN",0};
    reset(0,17);assert(!xxd_main(2,args));
    assert(same(output,"00000000: 4142 4344 4546 4748 494a 4b4c 4d4e 4f50  ABCDEFGHIJKLMNOP\n"
                       "00000010: 51                                       Q\n"));
    char normal[200];for (DWORD i=0;i<=used;i++)normal[i]=output[i];
    reset(0,17);chunk_limit=3;assert(!xxd_main(2,args) && same(output,normal));
    assert(closes==1 && reads>6);
    char *range[]={"xxd","-s0x2","-l","3","-c5","-g0","-u","DATA.BIN",0};
    reset(0,17);assert(!xxd_main(8,range));
    assert(same(output,"00000002: 434445      CDE\n"));
    char *plain[]={"xxd","-p","-c3","-l5","DATA.BIN",0};
    reset(0,17);assert(!xxd_main(5,plain) && same(output,"414243\n4445\n"));
    char *uneven[]={"xxd","-c5","-g3","-l2","DATA.BIN",0};
    reset(0,17);assert(!xxd_main(5,uneven));
    assert(same(output,"00000000: 4142         AB\n"));
    char *empty[]={"xxd","-l0","DATA.BIN",0};
    reset(0,17);assert(!xxd_main(3,empty) && !used && !reads && closes==1);
    empty[1]="-s99";reset(0,17);assert(!xxd_main(3,empty) && !used && !reads);
    reset(0,0);assert(!xxd_main(2,args) && !used && closes==1);
    for (unsigned i=0;i<6;i++)contents[i]=(BYTE[]){0,31,32,126,127,255}[i];
    char *binary[]={"xxd","-c6","-g1","DATA.BIN",0};
    reset(0,6);assert(!xxd_main(4,binary));
    assert(same(output,"00000000: 00 1f 20 7e 7f ff  .. ~..\n"));
    const char *bad[]={"-c0","-c65","-g65","-s-1","-l4294967296","-l0x100000000",
                       "-l12junk","-s0x","-r","-","-c"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        char *invalid[]={"xxd",(char*)bad[i],"DATA.BIN",0};reset(0,6);
        assert(xxd_main(3,invalid)==RM_ERR_ARGUMENT && !mounts);
    }
    for (int i=1;i<=6;i++) {
        reset(i,6);
        assert(xxd_main(8,range)!=0 && closes==(i>=3));
    }
    LW_ENV env={.size=sizeof(LW_ENV)};lw_env=&env;
    for (unsigned i=0;i<5;i++)env.cwd[i]="/BIN"[i];
    args[1]="../DATA.BIN";reset(0,6);assert(!xxd_main(2,args));
    char *dash[]={"xxd","--","-DATA.BIN",0};expected_path="/BIN/-DATA.BIN";
    reset(0,6);assert(!xxd_main(3,dash));
    /* Large inputs remain streamed; plain hex is exactly two chars per byte. */
    expected_path="/BIN/DATA.BIN";
    char *large[]={"xxd","-p","DATA.BIN",0};reset(0,sizeof(contents));
    assert(!xxd_main(3,large) && used==2*length+(length+29)/30 && closes==1);
    return 0;
}
