#ifndef _LW_APP_H
#define _LW_APP_H

#include "dev/blockdev.h"
#include "resman.h"

/* Load an LWP EXE from a filesystem path and create a background task.
 * Caller initializes rm_base and serializes filesystem/device access.
 * env may be NULL to use resman's default environment. TEXT applications
 * still require a registered text host. Files must be 1..1 MiB in size.
 * Returns RM_OK, an FS_* error, or an RM_* error (error ranges overlap).
 * On failure *task_id is zero and temporary resources are released.
 * The program path is copied to image.caption before the task can run;
 * paths over 255 bytes fail with RM_ERR_ARGUMENT.
 * On success the task owns its image; caller must eventually rm_task_reap
 * it, using rm_task_kill first if it is still running. No window is drawn.
 * task_id must point to an unused output variable, not a live task handle.
 */
int lw_app_launch(PBLKDEV device, PCSTR path, PCLW_ENV env, PDWORD task_id);

#endif
