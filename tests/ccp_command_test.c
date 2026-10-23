/* CCP cwd/cd/PATH, executable lookup, arguments and failure cleanup. */
#include <assert.h>
#define main ccp_main
#define printf ccp_printf
#include "../external/ccp.c"
#undef main
#undef printf

PVOID *lw_abi_base;
static int mode, mount_calls, reads, next_calls;
PVOID *rm_base;
PCLW_ENV lw_env;
static char opened_path[256], executable[32];
static PCSTR expected_command="./hello.exe",available_path;
static PCSTR expected_script;
static int open_calls;
static int allocated, loaded, runs, child_code;
static LW_ENV terminal_env;
static PVOID rm_table[LW_RM_SLOT_COUNT];
int fs_file_open(PFS_VOLUME volume,PCSTR path,PFS_FILE file) {
    (void)file;assert(volume->mounted);strcpy(opened_path,path);open_calls++;
    if (available_path && strcmp(path,available_path))return FS_ERR_NOT_FOUND;
    return mode==10 ? FS_ERR_NOT_FOUND : 0;
}
int fs_file_size(PFS_FILE file,PDWORD size) {
    (void)file;*size=sizeof(executable);return 0;
}
int fs_file_read(PFS_FILE file,PVOID bytes,DWORD size,PDWORD count) {
    (void)file;assert(bytes==executable && size==sizeof(executable));
    *count=mode==11 ? 0 : size;return 0;
}
int fs_file_close(PFS_FILE file) { (void)file;return 0; }
static PVOID allocate(DWORD size,DWORD align,DWORD tag) {
    (void)align;(void)tag;assert(size==sizeof(executable) && !allocated);
    if (mode==12)return 0;
    allocated=1;return executable;
}
static int release(PVOID bytes) {
    assert(bytes==executable && allocated);allocated=0;return 0;
}
static int load_image(PCVOID bytes,DWORD size,WORD owner,PRM_IMAGE *image) {
    assert(bytes==executable && size==sizeof(executable) && owner==RM_OWNER_SYSTEM);
    if (mode==13)return RM_ERR_FORMAT;
    loaded=1;*image=(PRM_IMAGE)executable;return 0;
}
static int run_image(PRM_IMAGE image,PCLW_ENV env,int *code) {
    assert(image==(PRM_IMAGE)executable && loaded && !allocated);
    assert(env->vi.txt==terminal_env.vi.txt && env->vi.window_id==71);
    assert(env->argc>=1 && !strcmp(env->argv[0],expected_command));
    assert(env->argv[env->argc]==0 && !strcmp(env->cwd,cwd));
    if (expected_script) {
        assert(env->argc==2 && !strcmp(env->argv[1],expected_script));
    } else if (env->argc>1) {
        assert(env->argc==3 && !strcmp(env->argv[1],"one") && !strcmp(env->argv[2],"two"));
    }
    runs++;*code=child_code;return mode==14 ? RM_ERR_ARGUMENT : 0;
}
static int unload_image(PRM_IMAGE image) {
    assert(image==(PRM_IMAGE)executable && loaded);loaded=0;return 0;
}
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
int fs_lookup(PFS_VOLUME volume,PCSTR path,PSFT_T entry) {
    assert(volume->mounted && *path=='/');
    if (mode==21)return FS_ERR_NOT_FOUND;
    entry->attr=mode==20 ? 0 : 0x10;return 0;
}
static void reset(int selected) {
    mode=selected;used=0;output[0]=0;
    reads=next_calls=mount_calls=0;
    strcpy(cwd,"/");strcpy(search_path,"/BIN");
    available_path=0;open_calls=0;expected_command="./hello.exe";
    pmt_p="";
}
int main(void) {
    rm_table[LW_RM_SLOT_ALLOC]=(PVOID)allocate;
    rm_table[LW_RM_SLOT_FREE]=(PVOID)release;
    rm_table[LW_RM_SLOT_IMAGE_LOAD]=(PVOID)load_image;
    rm_table[LW_RM_SLOT_IMAGE_RUN]=(PVOID)run_image;
    rm_table[LW_RM_SLOT_IMAGE_UNLOAD]=(PVOID)unload_image;
    rm_base=rm_table;
    terminal_env.size=sizeof(terminal_env);terminal_env.vi.window_id=71;lw_env=&terminal_env;
    abi_table[LW_SLOT_BLK_GET]=(PVOID)get_device;
    lw_abi_base=abi_table;
    char path[256];
    strcpy(cwd,"/BIN/SUB");
    assert(!program_path("../.././hello.exe",path) && !strcmp(path,"/hello.exe"));
    assert(!program_path("../APP//hello.exe",path) && !strcmp(path,"/BIN/APP/hello.exe"));
    assert(!program_path("/../../hello.exe",path) && !strcmp(path,"/hello.exe"));
    char huge[300];for (int i=0;i<299;i++)huge[i]='a';huge[299]=0;
    assert(program_path(huge,path)==RM_ERR_ARGUMENT);
    for (int i=0;i<3;i++) {
        reset(0);char arguments[]=" one\ttwo  ";pmt_p=arguments;child_code=42;
        assert(execute("./hello.exe")==42 && !strcmp(opened_path,"/hello.exe"));
        assert(!allocated && !loaded);
    }
    assert(runs==3);child_code=0;
    const int failures[]={1,10,11,12,13,14};
    for (unsigned int i=0;i<sizeof(failures)/sizeof(failures[0]);i++) {
        reset(failures[i]);assert(execute("./hello.exe")!=0);
        assert(!allocated && !loaded);
    }
    reset(0);expected_command="hello";available_path="/BIN/hello.EXE";
    assert(execute("hello")==0 && open_calls==2);
    reset(0);expected_command="hello.exe";
    assert(execute("hello.exe")==0 && !strcmp(opened_path,"/hello.exe"));
    reset(0);expected_command="./hello";
    assert(execute("./hello")==0 && !strcmp(opened_path,"/hello.EXE"));
    reset(0);available_path="/APP/tool.EXE";expected_command="tool";
    strcpy(search_path,"/BIN:/APP");
    assert(execute("tool")==0 && open_calls==3);
    reset(0);available_path="/BIN/hello.EXE";
    assert((int)execute("./hello")==FS_ERR_NOT_FOUND && open_calls==1);
    reset(0);pmt_p="/BIN:/APP";
    assert(execute("path")==0 && !strcmp(search_path,"/BIN:/APP"));
    pmt_p="/BAD:";assert((int)execute("path")==RM_ERR_ARGUMENT);
    assert(!strcmp(search_path,"/BIN:/APP"));
    pmt_p="-";assert(execute("path")==0 && !*search_path);
    pmt_p="./TOOLS:../APP";strcpy(cwd,"/BIN");
    assert(execute("path")==0 && !strcmp(search_path,"/BIN/TOOLS:/APP"));
    reset(0);pmt_p="/BIN";assert(execute("cd")==0 && !strcmp(cwd,"/BIN"));
    pmt_p="..";assert(execute("cd")==0 && !strcmp(cwd,"/"));
    pmt_p="";assert(execute("cd")==0 && !strcmp(cwd,"/BIN"));
    pmt_p="/FILE";mode=20;
    assert(execute("cd")!=0 && !strcmp(cwd,"/BIN"));
    pmt_p="/MISSING";mode=21;
    assert(execute("cd")!=0 && !strcmp(cwd,"/BIN"));
    pmt_p="/BIN /APP";mode=0;assert((int)execute("cd")==RM_ERR_ARGUMENT);
    reset(0);char quoted[]=" 'one' \"two\"";pmt_p=quoted;
    assert(execute("./hello.exe")==0);
    reset(0);char script[]=" 's/\\(old value\\)/new &/'";pmt_p=script;
    expected_script="s/\\(old value\\)/new &/";
    assert(execute("./hello.exe")==0);expected_script=0;
    reset(0);char bad_quote[]=" 'unterminated";pmt_p=bad_quote;
    assert((int)execute("./hello.exe")==RM_ERR_ARGUMENT && !mount_calls);
    return 0;
}
