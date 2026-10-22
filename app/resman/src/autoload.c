#include "../include/resman.h"
#include "../include/keyboard.h"
#include "abi.h"
#include "fs.h"

/* Retain the diagnostic after monitor redraws the text screen. */
int ghost_autoload_result;

/* Boot-only I/O: use the ABI's existing device, never reprobe/reset it while
 * monitor retains its mounted volume. Nothing is shared with the host task. */
int resman_autoload_ghost(void)
{
    FS_VOLUME volume = {0};
    FS_FILE file = {0};
    PRM_IMAGE image = 0;
    PVOID buffer = 0;
    DWORD length = 0, read_count = 0, id = 0;
    PCSTR stage = "MOUNT";
    int rc = RM_ERR_ABI;

    if (!lw_abi_base[LW_SLOT_BLK_GET])
        goto done;
    rc = fs_mount(&volume, lw_blk_get(0));
    if (rc) goto done;
    stage = "OPEN";
    rc = fs_file_open(&volume, "/GHOST.EXE", &file);
    if (rc) goto done;
    stage = "SIZE";
    rc = fs_file_size(&file, &length);
    if (rc) goto done;
    if (length < sizeof(LwpHdr) || length > 0x7ffff000u) {
        rc = RM_ERR_FORMAT;
        goto done;
    }
    stage = "ALLOC";
    buffer = kmalloc((int)length);
    if (!buffer || buffer == (PVOID)-1 || buffer == (PVOID)-2) {
        buffer = 0;
        rc = RM_ERR_MEMORY;
        goto done;
    }
    stage = "READ";
    rc = fs_file_read(&file, buffer, length, &read_count);
    if (rc) goto done;
    if (read_count != length) {
        rc = FS_ERR_IO;
        goto done;
    }
    fs_file_close(&file);
    stage = "IMAGE";
    rc = resman_image_load(buffer, length, RM_OWNER_SYSTEM, &image);
    if (rc) goto done;
    kfree(buffer);
    buffer = 0;

    rc = resman_image_set_caption(image, "/GHOST.EXE");
    if (rc) goto done;

    /* No task may run between publication of READY and input handoff. */
    DWORD flags = mem_irq_save();
    stage = "TASK";
    rc = resman_task_create(image, 0, &id);
    if (!rc) {
        memcpy(task[id].name, "GHOST.EXE", 10);
        stage = "KEYBOARD";
        rc = resman_keyboard_set_owner(id);
        if (rc) {
            /* kill/reap owns cleanup after the image becomes task-bound. */
            resman_task_kill(id);
            int exit_code;
            resman_task_reap(id, &exit_code);
        }
        image = 0;
    }
    mem_irq_restore(flags);

done:
    ghost_autoload_result = rc;
    fs_file_close(&file);
    if (buffer) kfree(buffer);
    if (image) resman_image_unload(image);
    if (rc) {
        lw_puts("GHOST AUTOLOAD FAILED: ");
        lw_puts(stage);
        lw_puts(" CODE=");
        lw_put_dword((DWORD)rc);
        lw_puts("\n\r");
    }
    return rc;
}
