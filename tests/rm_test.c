#include <assert.h>
#define main rm_main
#define printf rm_printf
#include "../external/coreutils/rm.c"
#undef main
#undef printf

PVOID *lw_abi_base;
PCLW_ENV lw_env;
static BLKDEV device;
static PVOID table[LW_SLOT_COUNT];
static int mode,mounts,calls,diagnostics;
static char paths[4][256];
static void copy(char *d,const char *s) {while ((*d++=*s++));}
static int equal(const char *a,const char *b) {
    while (*a && *a==*b) {a++;b++;}return *a==*b;
}
int rm_printf(const char *fmt,...) {(void)fmt;diagnostics++;return 0;}
static PBLKDEV get_device(DWORD index) {assert(!index);return &device;}
int fs_mount(PFS_VOLUME fs,PBLKDEV dev) {
    assert(dev==&device);mounts++;fs->mounted=1;
    return mode==1 ? FS_ERR_IO : 0;
}
int fs_remove(PFS_VOLUME fs,PCSTR path) {
    assert(fs->mounted && calls<4);copy(paths[calls++],path);
    return mode==4 && calls==1 ? FS_ERR_IO : 0;
}
static void reset(int selected) {mode=selected;mounts=calls=diagnostics=0;}
int main(void) {
    table[LW_SLOT_BLK_GET]=(PVOID)get_device;lw_abi_base=table;
    LW_ENV env={.size=sizeof(LW_ENV)};copy(env.cwd,"/BIN");lw_env=&env;
    char *args[]={"rm","A.TXT","../B.TXT",0};
    reset(0);assert(rm_main(3,args)==0 && mounts==1 && calls==2 && !diagnostics);
    assert(equal(paths[0],"/BIN/A.TXT") && equal(paths[1],"/B.TXT"));
    for (int i=1;i<=4;i+=3) {
        reset(i);assert(rm_main(3,args)!=0 && diagnostics);
        assert(calls==(i==4 ? 2 : 0));
    }
    reset(0);assert(rm_main(1,args)==RM_ERR_ARGUMENT && !mounts);
    args[2]="-a";assert(rm_main(3,args)==RM_ERR_ARGUMENT && !mounts && !calls);
    char *literal[]={"rm","--","-A.TXT",0};
    reset(0);assert(rm_main(3,literal)==0 && equal(paths[0],"/BIN/-A.TXT"));
    reset(0);assert(rm_main(2,literal)==RM_ERR_ARGUMENT && !mounts);
    args[1]="/NEW/";reset(0);
    assert(rm_main(2,args)==0 && equal(paths[0],"/NEW"));
    char *protected[]={"/",".","..","/bin","/BIN/../",""};
    for (unsigned i=0;i<sizeof(protected)/sizeof(protected[0]);i++) {
        args[1]=protected[i];reset(0);
        assert(rm_main(2,args)!=0 && !calls);
    }
    args[1]="";reset(0);assert(rm_main(2,args)!=0 && !calls);
    lw_env=0;args[1]="A.TXT";reset(0);
    assert(rm_main(2,args)==0 && equal(paths[0],"/A.TXT"));
    return 0;
}
