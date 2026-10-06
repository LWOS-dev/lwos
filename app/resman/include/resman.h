#ifndef _RESMAN_H
#define _RESMAN_H

#include "../../../lib/include/resman.h"
#include "lwp.h"

#define RM_MAX_IMAGES 32u

enum {
    RM_IMAGE_FREE = 0,
    RM_IMAGE_LOADED = 1,
    RM_IMAGE_RUNNING = 2
};

struct _RM_IMAGE {
    PVOID base;
    DWORD image_size, mem_size;
    DWORD entry, flags;
    PVOID stack;
    DWORD stack_size;
    WORD owner, state;
};

extern const PVOID lw_rm[LW_RM_SLOT_COUNT];

int resman_main(PVOID *abi);

int resman_init(void);
DWORD resman_version(void);

PVOID resman_alloc(DWORD size, DWORD align, DWORD tag);
int resman_free(PVOID p);

int resman_image_load(PCVOID file, DWORD file_size, WORD owner, PRM_IMAGE *out);
int resman_image_run(PRM_IMAGE image, PCLW_ENV env, int *exit_code);
int resman_image_unload(PRM_IMAGE image);

#endif
