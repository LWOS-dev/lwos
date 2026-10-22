/* Host regression: argument handling, ownership transfer and failure cleanup. */
#include <assert.h>
#include "app.h"
#include "fs.h"
static PVOID slots[LW_RM_SLOT_COUNT];
PVOID *rm_base=slots;
static BLKDEV device;
static LW_ENV environment;
static BYTE buffer[128], image_token;
static int fail, freed, unloaded, closed;
static DWORD length;
int fs_mount(PFS_VOLUME fs, PBLKDEV d) {
    (void)fs; assert(d==&device); return fail==1 ? FS_ERR_IO : 0;
}
int fs_file_open(PFS_VOLUME fs, PCSTR path, PFS_FILE file) {
    (void)fs; (void)file; assert(path[0]=='/'); return fail==2 ? FS_ERR_NOT_FOUND : 0;
}
int fs_file_size(PFS_FILE file, PDWORD size) {
    (void)file; *size=length; return fail==3 ? FS_ERR_IO : 0;
}
int fs_file_read(PFS_FILE file, PVOID bytes, DWORD size, PDWORD count) {
    (void)file; assert(bytes==buffer && size==length);
    *count=fail==6 ? size-1 : size; return fail==5 ? FS_ERR_IO : 0;
}
int fs_file_close(PFS_FILE file) { (void)file; closed++; return 0; }
static PVOID alloc_bytes(DWORD size, DWORD align, DWORD tag) {
    assert(size==length && !align && !tag); return fail==4 ? 0 : buffer;
}
static int free_bytes(PVOID p) { assert(p==buffer); freed++; return 0; }
static int load_image(PCVOID bytes, DWORD size, WORD owner, PRM_IMAGE *image) {
    assert(bytes==buffer && size==length && owner==RM_OWNER_SYSTEM);
    if (fail==7)return RM_ERR_FORMAT;
    *image=(PRM_IMAGE)&image_token; return 0;
}
static int unload_image(PRM_IMAGE image) {
    assert(image==(PRM_IMAGE)&image_token); unloaded++; return 0;
}
static int create_task(PRM_IMAGE image, PCLW_ENV env, PDWORD id) {
    assert(image==(PRM_IMAGE)&image_token && env==&environment);
    *id=12; return fail==8 ? RM_ERR_NOT_READY : 0;
}
int main(void) {
    slots[LW_RM_SLOT_ALLOC]=(PVOID)alloc_bytes;
    slots[LW_RM_SLOT_FREE]=(PVOID)free_bytes;
    slots[LW_RM_SLOT_IMAGE_LOAD]=(PVOID)load_image;
    slots[LW_RM_SLOT_IMAGE_UNLOAD]=(PVOID)unload_image;
    slots[LW_RM_SLOT_TASK_CREATE]=(PVOID)create_task;
    DWORD id=99;
    assert(lw_app_launch(&device,"/OTHER.EXE",0,0)==RM_ERR_ARGUMENT);
    assert(lw_app_launch(0,"/OTHER.EXE",0,&id)==RM_ERR_ARGUMENT && !id);
    assert(lw_app_launch(&device,0,0,&id)==RM_ERR_ARGUMENT && !id);
    assert(lw_app_launch(&device,"",0,&id)==RM_ERR_ARGUMENT && !id);
    for (fail=0; fail<=8; fail++) {
        length=128; freed=unloaded=closed=0; id=99;
        int rc=lw_app_launch(&device,"/OTHER.EXE",&environment,&id);
        assert(fail ? (rc!=0 && id==0) : (rc==0 && id==12));
        assert(closed==1);
        assert(freed==(!fail || fail>=5));
        assert(unloaded==(fail==8));
    }
    fail=0;
    for (int i=0; i<2; i++) {
        length=i ? 0x100001 : 0; freed=unloaded=0;
        assert(lw_app_launch(&device,"/OTHER.EXE",0,&id)==RM_ERR_FORMAT);
        assert(!id && !freed && !unloaded);
    }
    return 0;
}
