/* Standalone ls: short options, FAT traversal and error propagation. */
#include <assert.h>
#define main ls_main
#define printf ccp_printf
#include "../external/coreutils/ls.c"
#undef main
#undef printf
PVOID *lw_abi_base;
PCLW_ENV lw_env;
static int mode,mount_calls,reads,next_calls;
static PCSTR expected_path="/";
static char output[4096];
static unsigned int used;
static BLKDEV device;
static PVOID abi_table[LW_SLOT_COUNT];

int ccp_printf(const char *format, ...) {
    va_list args;
    va_start(args,format);
    int n=vsnprintf(output+used,sizeof(output)-used,format,args);
    va_end(args);
    assert(n>=0 && (unsigned int)n<sizeof(output)-used);
    used+=(unsigned int)n;
    return n;
}
static PBLKDEV get_device(DWORD index) {
    assert(index==0);
    return &device;
}
int fs_mount(PFS_VOLUME fs,PBLKDEV dev) {
    assert(dev==&device);
    mount_calls++;
    if (mode==1)return FS_ERR_IO;
    fs->mounted=1;
    fs->cluster_count=2;
    fs->sectors_per_cluster=1;
    return 0;
}
int fs_lookup(PFS_VOLUME fs,PCSTR path,PSFT_T out) {
    assert(fs->mounted && !strcmp(path,expected_path));
    if (mode==2)return FS_ERR_NOT_FOUND;
    out->cluster=5;
    out->attr=mode==3 ? 0 : 0x10;
    return 0;
}
int fat_read_fte(PFS_VOLUME fs,DWORD cluster,int index,PSFT_T out) {
    (void)fs;
    reads++;
    if (mode==4)return FS_ERR_IO;
    if (mode==5)return FS_ENTRY_END;
    if (mode==6)return FS_ENTRY_SKIP; /* Cycle bound must stop this. */
    if (cluster==5 && index==0) {
        strcpy(out->name,"README  TXT");out->attr=0x20;
        return FS_ENTRY_VALID;
    }
    if (cluster==5 && index==2) {
        strcpy(out->name,"RES        ");out->attr=0x10;
        return FS_ENTRY_VALID;
    }
    if (cluster==7 && index==0) {
        strcpy(out->name,"LONGNAMEBIN");out->attr=0x20;
        return FS_ENTRY_VALID;
    }
    if (cluster==7)return FS_ENTRY_END;
    return FS_ENTRY_SKIP; /* Deleted, LFN and volume entries are skipped. */
}
int next_cluster(PFS_VOLUME fs,DWORD current,PDWORD next) {
    (void)fs;
    next_calls++;
    assert(current==5 || current==7);
    if (mode==7)return FS_ERR_IO;
    if (mode==8)return 0;
    *next=mode==6 ? 5 : 7;
    return 1;
}
static void reset(int selected) {
    mode=selected;used=0;output[0]=0;
    reads=next_calls=mount_calls=0;
}
int main(void) {
    abi_table[LW_SLOT_BLK_GET]=(PVOID)get_device;lw_abi_base=abi_table;
    char *plain[]={"ls",0};
    reset(0);assert(ls_main(1,plain)==0);
    assert(!strcmp(output,"README.TXT\nRES\nLONGNAME.BIN\n") && next_calls==1);
    char *valid[][4]={{"ls","-l",0},{"ls","-ll",0},{"ls","-l","-l",0},{"ls","-l","--",0}};
    for (unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++) {
        int argc=0;while (valid[i][argc])argc++;
        reset(0);assert(ls_main(argc,valid[i])==0 && next_calls==1);
        assert(!strcmp(output,"README.TXT   \nRES          <DIR>\nLONGNAME.BIN \n"));
    }
    char *end[]={"ls","--",0};
    reset(0);assert(ls_main(2,end)==0 && !strcmp(output,"README.TXT\nRES\nLONGNAME.BIN\n"));
    char *invalid[][4]={{"ls","-la",0},{"ls","-a",0},{"ls","-",0},
        {"ls","/RES",0},{"ls","-l","/RES",0},{"ls","--","-l",0},
        {"ls","--long",0},{"ls","-lfoo",0},{"ls","-l","-a",0}};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        int argc=0;while (invalid[i][argc])argc++;
        reset(0);assert(ls_main(argc,invalid[i])==RM_ERR_ARGUMENT && !mount_calls && !used);
    }
    reset(1);assert(dir_list(0)==FS_ERR_IO && reads==0);
    reset(2);assert(dir_list(0)==FS_ERR_NOT_FOUND && reads==0);
    reset(3);assert(dir_list(0)==FS_ERR_ARGUMENT && reads==0);
    reset(4);assert(dir_list(0)==FS_ERR_IO && next_calls==0);
    reset(5);assert(dir_list(0)==0 && used==0 && next_calls==0);
    reset(6);assert(dir_list(0)==FS_ERR_FORMAT && next_calls==2 && reads==32);
    reset(7);assert(dir_list(0)==FS_ERR_IO);
    reset(8);assert(dir_list(0)==0 && next_calls==1);
    LW_ENV env={.size=sizeof(LW_ENV)};lw_env=&env;
    strcpy(env.cwd,"/BIN");expected_path="/BIN";
    reset(0);assert(ls_main(1,plain)==0);
    env.size=LW_ENV_BASE_SIZE;expected_path="/";
    reset(0);assert(ls_main(1,plain)==0);
    return 0;
}
