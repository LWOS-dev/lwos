/* Exercise the actual file-manager execution flow with FAT and mocked services. */
#define kmalloc execute_alloc
#define kfree execute_free
#define fs_file_read execute_read
#define main hex_regression_main
#include "tui_hex_test.c"
#undef main
#undef fs_file_read
#undef kmalloc
#undef kfree

static BYTE file_buffer[256];
static WORD saved_screen[80*25];
static PVOID abi_slots[LW_SLOT_COUNT], rm_slots[LW_RM_SLOT_COUNT];
static int short_read;
int fs_file_read(PFS_FILE, PVOID, DWORD, PDWORD);
int execute_read(PFS_FILE f,PVOID p,DWORD n,PDWORD out) {
    int err=fs_file_read(f,p,n,out);
    if (!err && short_read && *out)(*out)--;
    return err;
}
static int alloc_fail, read_fail, load_fail, run_fail, unload_fail;
static int allocated, freed, loaded, ran, unloaded, restored, messages;
PVOID execute_alloc(int size) {
    assert(size>0 && size<=256);
    if (alloc_fail)return (PVOID)-1;
    allocated++;
    if (read_fail)fail_read=1;
    return file_buffer;
}
void execute_free(PVOID p) { assert(p==file_buffer); freed++; }
static DWORD getkey(void) { messages++; return 0; }
static void cursor(int value) { assert(value==0); }
static void restore(void) { restored++; memzero(screen,sizeof(screen)); }
static int load(PCVOID p,DWORD size,WORD owner,PRM_IMAGE *out) {
    assert(p==file_buffer && size==80 && owner==RM_OWNER_SYSTEM);
    loaded++;
    if (load_fail)return RM_ERR_FORMAT;
    *out=(PRM_IMAGE)1;
    return 0;
}
static int run(PRM_IMAGE img,PCLW_ENV env,int *code) {
    assert(img==(PRM_IMAGE)1 && !env && freed==ran+1);
    ran++;
    memzero(screen,sizeof(screen));
    *code=-42;
    return run_fail ? RM_ERR_ARGUMENT : 0;
}
static int unload(PRM_IMAGE img) {
    assert(img==(PRM_IMAGE)1); unloaded++;
    return unload_fail ? RM_ERR_ARGUMENT : 0;
}
static void setup(void) {
    fixture();
    subvideo=saved_screen;
    short_read=0;
    alloc_fail=read_fail=load_fail=run_fail=unload_fail=0;
    allocated=freed=loaded=ran=unloaded=restored=messages=0;
    FS_FILE f={0}; DWORD count;
    BYTE bytes[80]={0};
    assert(fs_touch(&monitor_fs,"/APP.EXE")==0);
    assert(fs_file_open(&monitor_fs,"/APP.EXE",&f)==0);
    assert(fs_file_write(&f,bytes,sizeof(bytes),&count)==0 && count==80);
    fs_file_close(&f);
    for(int i=0;i<2000;i++)screen[i]=0x1234;
    lw_abi_base=abi_slots; rm_base=rm_slots;
    abi_slots[LW_SLOT_CONSOLE_GETK]=(PVOID)getkey;
    abi_slots[LW_SLOT_IO_VIDEO_EXIT]=(PVOID)restore;
    abi_slots[LW_SLOT_CONSOLE_SET_CUR]=(PVOID)cursor;
    rm_slots[LW_RM_SLOT_IMAGE_LOAD]=(PVOID)load;
    rm_slots[LW_RM_SLOT_IMAGE_RUN]=(PVOID)run;
    rm_slots[LW_RM_SLOT_IMAGE_UNLOAD]=(PVOID)unload;
}
int main(void) {
    setup(); rm_base=0; fe_execute("/APP.EXE"); assert(!allocated && messages==1);
    setup(); alloc_fail=1; fe_execute("/APP.EXE"); assert(!loaded && !freed);
    setup(); read_fail=1; fe_execute("/APP.EXE"); assert(!loaded && freed==1);
    setup(); short_read=1; fe_execute("/APP.EXE"); assert(!loaded && freed==1);
    setup(); load_fail=1; fe_execute("/APP.EXE"); assert(loaded==1 && !ran && !unloaded && freed==1);
    setup(); run_fail=1; fe_execute("/APP.EXE"); assert(ran==1 && unloaded==1 && freed==1 && restored==1);
    setup(); unload_fail=1; fe_execute("/APP.EXE"); assert(unloaded==1 && messages==1);
    setup();
    for(int i=0;i<40;i++)fe_execute("/APP.EXE");
    assert(allocated==40 && freed==40 && loaded==40 && ran==40 && unloaded==40);
    assert(restored==40 && messages==40);
    for(int i=0;i<2000;i++)assert(screen[i]==0x1234);
    puts("PASS: execute failures, cleanup, repeated runs, video restoration");
}
