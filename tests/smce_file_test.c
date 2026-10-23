/* Actual SMCE file pipeline on an in-memory FAT volume. AI-assisted test. */
#include <assert.h>
#define main fs_fixture_main
#include "fs_dir_test.c"
#undef main

static FS_VOLUME mounted;
static int mount_error;
int smce_mount(PFS_VOLUME fs,PBLKDEV device);
#define fs_mount smce_mount
#define main smce_main
#include "../external/smce.c"
#undef main
#undef fs_mount

PVOID *lw_abi_base;
PCLW_ENV lw_env;
static PVOID slots[LW_SLOT_COUNT];
static PBLKDEV get_device(DWORD index) {assert(!index);return &dev;}
int smce_mount(PFS_VOLUME fs,PBLKDEV device) {
    assert(device==&dev);*fs=mounted;return mount_error;
}
static void save(PCSTR path,PCSTR text) {
    FS_FILE f={0};DWORD n;
    assert(fs_touch(&mounted,path)==0);
    assert(fs_file_open(&mounted,path,&f)==0);
    assert(fs_file_truncate(&f)==0);
    assert(fs_file_write(&f,text,strlen(text),&n)==0 && n==strlen(text));
    fs_file_close(&f);
}
int main(void) {
    fixture(&mounted);slots[LW_SLOT_BLK_GET]=(PVOID)get_device;lw_abi_base=slots;
    char *args[]={"smce","/IN.S","-o","/OUT.BIN",0};
    char source[800];
    source[0]='#';for (int i=1;i<600;i++)source[i]=' ';
    strcpy(source+600,"\n.org 40\nstart: movl $end,%eax\nmovl %eax,%ebx\njmp start\nend: .long start\n");
    save("/IN.S",source);
    for (int cycle=0;cycle<2;cycle++) {
        assert(smce_main(4,args)==0);
        FS_FILE f={0};BYTE code[64];DWORD n;
        assert(fs_file_open(&mounted,"/OUT.BIN",&f)==0);
        assert(fs_file_read(&f,code,sizeof(code),&n)==0 && n==56);
        for (int i=0;i<40;i++)assert(!code[i]);
        const BYTE tail[]={0xb8,52,0,0,0,0x89,0xc3,0xe9,0xf4,0xff,0xff,0xff,40,0,0,0};
        for (unsigned i=0;i<sizeof(tail);i++)assert(code[40+i]==tail[i]);
        fs_file_close(&f);
    }
    save("/IN.S","movl $1,%ax\n");
    assert(smce_main(4,args)!=0); /* First-pass syntax error must be returned. */
    FS_FILE f={0};DWORD n;
    assert(fs_file_open(&mounted,"/OUT.BIN",&f)==0);
    assert(fs_file_size(&f,&n)==0 && n==56); /* Validation precedes truncation. */
    fs_file_close(&f);
    mount_error=FS_ERR_IO;assert(smce_main(4,args)==FS_ERR_IO);mount_error=0;
    save("/IN.S","nop\n");
    read_fail=1;assert(smce_main(4,args)==FS_ERR_IO);read_fail=0;
    printf("PASS: SMCE file passes, forward references, padding, repeated runs and I/O errors\n");
    return 0;
}
