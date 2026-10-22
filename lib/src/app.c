#include "app.h"
#include "fs.h"

int lw_app_launch(PBLKDEV device, PCSTR path, PCLW_ENV env, PDWORD task_id) {
    FS_VOLUME volume={0};
    FS_FILE file={0};
    PRM_IMAGE image=0;
    PVOID bytes=0;
    DWORD size=0, count=0;
    DWORD created_id=0;
    int rc;
    if (!task_id)return RM_ERR_ARGUMENT;
    *task_id=0;
    if (!device || !path || !*path)return RM_ERR_ARGUMENT;
    rc=fs_mount(&volume,device);
    if (rc)goto done;
    rc=fs_file_open(&volume,path,&file);
    if (rc)goto done;
    rc=fs_file_size(&file,&size);
    if (rc)goto done;
    if (!size || size>0x100000u) {rc=RM_ERR_FORMAT;goto done;}
    bytes=rm_alloc(size,0,0);
    if (!bytes) {rc=RM_ERR_MEMORY;goto done;}
    rc=fs_file_read(&file,bytes,size,&count);
    if (rc)goto done;
    if (count!=size) {rc=RM_ERR_FORMAT;goto done;}
    rc=rm_image_load(bytes,size,RM_OWNER_SYSTEM,&image);
    if (rc)goto done;
    rc=rm_task_create(image,env,&created_id);
    if (!rc) {
        *task_id=created_id;
        image=0; /* The task owns the image until reap. */
    }
done:
    if (image)rm_image_unload(image);
    if (bytes)rm_free(bytes);
    fs_file_close(&file);
    return rc;
}
