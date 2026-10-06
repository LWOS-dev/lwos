#ifndef _LW_RESMAN_H
#define _LW_RESMAN_H

#include "stdint.h"

#define LW_RM_BASE 0x00180000u
#define LW_RM_LIMIT 0x00200000u
/* Called by monitor on its existing stack; returns an RM_* status. */
typedef int (*RESMAN_ENTRY)(PVOID *abi);
#define LW_RM_MAGIC 0x4241574cu
#define LW_RM_IDENT 0x4d52574cu
#define LW_ENV_VERSION 1u
#define RM_OWNER_SYSTEM 0u

enum {
    RM_OK = 0,
    RM_ERR_ARGUMENT = -1,
    RM_ERR_NOT_READY = -2,
    RM_ERR_MEMORY = -3,
    RM_ERR_FORMAT = -4,
    RM_ERR_ABI = -5,
    RM_ERR_RELOC = -6,
    RM_ERR_NOT_IMPLEMENTED = -7
};

enum {
    LW_RM_SLOT_MAGIC = 0x00,
    LW_RM_SLOT_ENTRY = 0x01,
    LW_RM_SLOT_BSS_START = 0x02,
    LW_RM_SLOT_BSS_END = 0x03,
    LW_RM_SLOT_STACK_TOP = 0x04,
    LW_RM_SLOT_IDENT = 0x05,
    LW_RM_SLOT_VERSION = 0x06,
    LW_RM_SLOT_ALLOC = 0x10,
    LW_RM_SLOT_FREE = 0x11,
    LW_RM_SLOT_IMAGE_LOAD = 0x40,
    LW_RM_SLOT_IMAGE_RUN = 0x41,
    LW_RM_SLOT_IMAGE_UNLOAD = 0x42,
    LW_RM_SLOT_COUNT = 0x43
};

typedef struct _LW_ENV {
    DWORD version, size;
    PVOID *abi;
    PVOID *rm;
    int argc;
    char **argv;
} LW_ENV, *PLW_ENV;
typedef const LW_ENV *PCLW_ENV;
typedef int (*LWP_ENTRY)(PCLW_ENV env);

typedef struct _RM_IMAGE RM_IMAGE, *PRM_IMAGE;

extern PVOID *rm_base;

int rm_attach(PVOID base);

#define RM_CALL(slot, type) ((type)rm_base[slot])
#define rm_version RM_CALL(LW_RM_SLOT_VERSION, DWORD (*)(void))
#define rm_alloc RM_CALL(LW_RM_SLOT_ALLOC, PVOID (*)(DWORD, DWORD, DWORD))
#define rm_free RM_CALL(LW_RM_SLOT_FREE, int (*)(PVOID))
#define rm_image_load RM_CALL(LW_RM_SLOT_IMAGE_LOAD, int (*)(PCVOID, DWORD, WORD, PRM_IMAGE *))
#define rm_image_run RM_CALL(LW_RM_SLOT_IMAGE_RUN, int (*)(PRM_IMAGE, PCLW_ENV, int *))
#define rm_image_unload RM_CALL(LW_RM_SLOT_IMAGE_UNLOAD, int (*)(PRM_IMAGE))

#endif
