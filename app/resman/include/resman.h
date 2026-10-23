#ifndef _RESMAN_H
#define _RESMAN_H

#include "../../../lib/include/resman.h"
#include "lwp.h"
#include "task.h"

#define RM_MAX_IMAGES 32u

enum {
    RM_IMAGE_FREE = 0,
    RM_IMAGE_LOADING = 1,
    RM_IMAGE_LOADED = 2,
    RM_IMAGE_RUNNING = 3,
    RM_IMAGE_BOUND = 4
};

struct _RM_IMAGE {
    PVOID base;
    DWORD image_size, mem_size;
    DWORD entry, flags;
    PVOID stack;
    DWORD stack_size;
    WORD owner, state;
    LW_BUF_TXT text;
    DWORD window_id, text_task;
    LW_WINDOW_EVENT events[LW_WINDOW_QUEUE_SIZE];
    DWORD event_head, event_tail;
    CHAR caption[LW_WINDOW_CAPTION_SIZE];
};

extern const PVOID lw_rm[LW_RM_SLOT_COUNT];

int resman_main(PVOID *abi);
int resman_autoload_ghost(void);

int resman_init(void);
DWORD resman_version(void);
int resman_sleep(DWORD milliseconds);

PVOID resman_alloc(DWORD size, DWORD align, DWORD tag);
int resman_free(PVOID p);
/* Private: reap calls this before allowing a task ID to be reused. */
void resman_alloc_reap(DWORD owner);

void resman_text_task_gone(DWORD id);
int resman_text_host(int enable);
int resman_text_available(void);
int resman_text_snapshot(DWORD slot, RM_TEXT_SNAPSHOT *out);
int resman_text_snapshot_caption(DWORD slot, RM_TEXT_SNAPSHOT *out);
int resman_image_set_caption(PRM_IMAGE image, PCSTR caption);
int resman_window_set_caption(DWORD window, PCSTR caption);

int resman_window_send(DWORD window, const LW_WINDOW_EVENT *event);
int resman_window_poll(DWORD window, LW_WINDOW_EVENT *event);
int resman_window_wait(DWORD window);

int resman_image_acquire(PRM_IMAGE *out);
int resman_image_load(PCVOID file, DWORD file_size, WORD owner, PRM_IMAGE *out);
int resman_image_run(PRM_IMAGE image, PCLW_ENV env, int *exit_code);
int resman_image_unload(PRM_IMAGE image);
int resman_image_valid(PRM_IMAGE pimg);
/* Private: release synchronous child images after their caller task dies. */
void resman_image_reap_calls(DWORD task_id);

void resman_alarm(void);
void resman_pcspk(int hz);
void resman_pcspk_shut(void);
int resman_keyboard_claim(void);
int resman_keyboard_release(void);
int resman_keyboard_own(void);

void* resman_task_table(void);
int resman_task_acquire(PTASK *out);
int resman_task_create(PRM_IMAGE image, PCLW_ENV env, PDWORD out_id);

int resman_task_exit(int exit_code);
int resman_task_kill(DWORD id);
int resman_task_reap(DWORD id, int *exit_code);

#endif
