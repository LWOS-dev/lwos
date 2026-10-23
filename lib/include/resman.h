#ifndef _LW_RESMAN_H
#define _LW_RESMAN_H

#include "stdint.h"
#include "keys.h"
#include "window.h"

#define LW_RM_BASE 0x00180000u
#define LW_RM_LIMIT 0x00200000u
/* Called by monitor on its existing stack; returns an RM_* status. */
typedef int (*RESMAN_ENTRY)(PVOID *abi);
#define LW_RM_MAGIC 0x4241574cu
#define LW_RM_IDENT 0x4d52574cu
#define LW_ENV_VERSION 1u
#define RM_OWNER_SYSTEM 0u
#define RM_TASK_EXIT_KILLED (-128)

enum {
    RM_OK = 0,
    RM_ERR_ARGUMENT = -1,
    RM_ERR_NOT_READY = -2,
    RM_ERR_MEMORY = -3,
    RM_ERR_FORMAT = -4,
    RM_ERR_ABI = -5,
    RM_ERR_RELOC = -6,
    RM_ERR_NOT_IMPLEMENTED = -7,
    RM_ERR_TIMEOUT = -8
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
    LW_RM_SLOT_IMAGE_ACQUIRE = 0x40,
    LW_RM_SLOT_IMAGE_LOAD = 0x41,
    LW_RM_SLOT_IMAGE_RUN = 0x42,
    LW_RM_SLOT_IMAGE_UNLOAD = 0x43,
    LW_RM_SLOT_ALARM = 0x50,
    LW_RM_SLOT_ALARM_FREQ = 0x51,
    LW_RM_SLOT_ALARM_SHUT = 0x52,
    LW_RM_SLOT_TASK_TABLE = 0x53,
    LW_RM_SLOT_TASK_CURRENT = 0x54,
    LW_RM_SLOT_TASK_ACQUIRE = 0x55,
    LW_RM_SLOT_TASK_CREATE = 0x56,
    LW_RM_SLOT_TASK_EXIT = 0x57,
    LW_RM_SLOT_TASK_KILL = 0x58,
    LW_RM_SLOT_TASK_REAP = 0x59,
    LW_RM_SLOT_ENABLE_INPUT = 0x60,
    LW_RM_SLOT_INPUT_WAIT,
    LW_RM_SLOT_GETMPOLL,
    LW_RM_SLOT_GETMPOS,
    LW_RM_SLOT_TEXT_HOST,
    LW_RM_SLOT_TEXT_SNAPSHOT,
    LW_RM_SLOT_WINDOW_SEND,
    LW_RM_SLOT_WINDOW_POLL,
    LW_RM_SLOT_WINDOW_WAIT,
    LW_RM_SLOT_SLEEP,
    LW_RM_SLOT_UPTIME_MS,
    LW_RM_SLOT_IMAGE_SET_CAPTION,
    LW_RM_SLOT_WINDOW_SET_CAPTION,
    LW_RM_SLOT_TEXT_SNAPSHOT_CAPTION,
    LW_RM_SLOT_NET_OPEN,
    LW_RM_SLOT_NET_RECV,
    LW_RM_SLOT_NET_SEND,
    LW_RM_SLOT_NET_CLOSE,
    LW_RM_SLOT_NET_INFO,
    LW_RM_SLOT_COUNT
};

typedef struct _LW_BUF_WIN {
    DWORD canva_w;
    DWORD canva_h;
    DWORD pitch;
    DWORD format;
    PVOID buffer;
} LW_BUF_WIN, *PLW_BUF_WIN;
typedef struct _LW_BUF_TXT {
    DWORD cols;
    DWORD lines;
    PVOID buffer;
    DWORD cursor; // cursor pos
} LW_BUF_TXT, *PLW_BUF_TXT;

enum {
    LW_VI_LEGACY=0,
    LW_VI_NONE,
    LW_VI_TEXT,
    LW_VI_GFX
};

typedef struct _LW_VI {
    DWORD size;
    DWORD mode;
    DWORD window_id;
    PLW_BUF_WIN win;
    PLW_BUF_TXT txt;
} LW_VI, *PLW_VI;

/* C0: copied under the resman lock; no borrowed pointers escape to GHOST. */
#define LW_TEXT_COLS 80u
#define LW_TEXT_LINES 25u
typedef struct _RM_TEXT_SNAPSHOT {
    DWORD window_id, task_id, task_state;
    int exit_code;
    WORD cells[LW_TEXT_COLS * LW_TEXT_LINES];
    CHAR caption[LW_WINDOW_CAPTION_SIZE];
    DWORD cursor;
} RM_TEXT_SNAPSHOT;

typedef struct _LW_ENV {
    DWORD version, size;
    PVOID *abi;
    PVOID *rm;
    int argc;
    char **argv;
    LW_VI vi; // visible
    CHAR cwd[256]; // optional v1 tail: absolute working directory (empty means /)
} LW_ENV, *PLW_ENV;
typedef const LW_ENV *PCLW_ENV;
#define LW_ENV_BASE_SIZE __builtin_offsetof(LW_ENV,cwd)
static inline PCSTR lw_env_cwd(PCLW_ENV env) {
    return env && env->size>=sizeof(LW_ENV) && env->cwd[0] ? env->cwd : "/";
}
typedef int (*LWP_ENTRY)(PCLW_ENV env);

typedef struct _RM_IMAGE RM_IMAGE, *PRM_IMAGE;

typedef struct _RM_MOUSE_POS { int x, y; } RM_MOUSE_POS;
/* Input belongs to the keyboard owner. Poll returns VK_NONE when empty,
 * keyboard codes or mouse VK_*BUTTON (release: VK_KEYUP | button).
 * Position is in pixels, origin top-left, clamped to framebuffer bounds.
 * getmpos acknowledges pending movement; drain poll and read position before
 * waiting. wait atomically sleeps until keyboard, buttons or movement arrive.
 * Enable is idempotent, returns RM_OK or RM_ERR_NOT_READY. */
#define rm_enable_input RM_CALL(LW_RM_SLOT_ENABLE_INPUT, int (*)(void))
#define rm_input_wait RM_CALL(LW_RM_SLOT_INPUT_WAIT, void (*)(void))
#define rm_getmpoll RM_CALL(LW_RM_SLOT_GETMPOLL, int (*)(void))
#define rm_getmpos RM_CALL(LW_RM_SLOT_GETMPOS, int (*)(RM_MOUSE_POS *))

extern PVOID *rm_base;

int rm_attach(PVOID base);

#define RM_CALL(slot, type) ((type)rm_base[slot])
#define rm_version              RM_CALL(LW_RM_SLOT_VERSION,         DWORD (*)(void))
/* Page-granular, uninitialized memory. size > 0; align=0 or power-of-two
 * <=4096. NULL on failure. tag is recorded for diagnostics, not an owner ID.
 * free(NULL) succeeds; otherwise only an owned, live allocation base is valid.
 * Background allocations are collected at task reap. F4 shares task 0 and
 * must explicitly free its allocations. These checks do not isolate memory. */
// rm_alloc(int size, int ailgn, int tag)
#define rm_alloc                RM_CALL(LW_RM_SLOT_ALLOC,           PVOID (*)(DWORD, DWORD, DWORD))
#define rm_free                 RM_CALL(LW_RM_SLOT_FREE,            int (*)(PVOID))
#define rm_image_load           RM_CALL(LW_RM_SLOT_IMAGE_LOAD,      int (*)(PCVOID, DWORD, WORD, PRM_IMAGE *))
/* Synchronous call on the caller's task, returning the entry's exit code.
 * TEXT images require env->vi to identify the caller's existing text window;
 * the window and input queue are borrowed until return. No task is created.
 * The caller retains image ownership and must unload it after return. */
#define rm_image_run            RM_CALL(LW_RM_SLOT_IMAGE_RUN,       int (*)(PRM_IMAGE, PCLW_ENV, int *))
#define rm_image_unload         RM_CALL(LW_RM_SLOT_IMAGE_UNLOAD,    int (*)(PRM_IMAGE))
#define rm_task_create          RM_CALL(LW_RM_SLOT_TASK_CREATE,     int (*)(PRM_IMAGE, PCLW_ENV, PDWORD))
/* exit: success never returns; monitor/foreground calls return an error.
 * kill: stop another live task. reap: nonblocking, DEAD only; returns its
 * exit code and releases image, stack, GDT and slot. IDs are reusable slots. */
#define rm_task_exit            RM_CALL(LW_RM_SLOT_TASK_EXIT,       int (*)(int))
#define rm_task_kill            RM_CALL(LW_RM_SLOT_TASK_KILL,       int (*)(DWORD))
#define rm_task_reap            RM_CALL(LW_RM_SLOT_TASK_REAP,       int (*)(DWORD, int *))
#define rm_alarm                RM_CALL(LW_RM_SLOT_ALARM,           void (*)(void))


/* Host registration belongs to the calling task. Snapshot index is an image
 * slot (0..31); missing/non-text slots return RM_ERR_NOT_READY.
 * The caption snapshot uses a new slot; the legacy slot keeps its original
 * 4016-byte layout for already-built callers. Rebuild to access caption. */
#define rm_text_host RM_CALL(LW_RM_SLOT_TEXT_HOST, int (*)(int))
#define rm_text_snapshot RM_CALL(LW_RM_SLOT_TEXT_SNAPSHOT_CAPTION, int (*)(DWORD, RM_TEXT_SNAPSHOT *))

/* Only the registered host may send to a live TEXT window. Window IDs are
 * generation IDs, not reusable task slots. Full queues return NOT_READY
 * without overwriting earlier events. No focus policy is kept in resman.
 * poll: 1 event, 0 empty, negative RM_* error; caller owns the window.
 * wait: atomically sleep until that caller's window has an event.
 */
#define rm_window_send RM_CALL(LW_RM_SLOT_WINDOW_SEND, int (*)(DWORD, const LW_WINDOW_EVENT *))
#define rm_window_poll RM_CALL(LW_RM_SLOT_WINDOW_POLL, int (*)(DWORD, LW_WINDOW_EVENT *))
#define rm_window_wait RM_CALL(LW_RM_SLOT_WINDOW_WAIT, int (*)(DWORD))

/* Blocking millisecond sleep; 0 yields. Requires a running task and IF=1.
 * Positive sleeps never intentionally return early, but scheduling/IRQ delays
 * can make them longer. A phase guard adds up to one timer tick (about 1 ms).
 * Maximum is under half the 32-bit tick range for wrap-safe deadlines.
 * uptime_ms wraps after ~49.7 days; subtract unsigned values for intervals.
 */
#define RM_SLEEP_MAX_MS 0x7ffffffdu
#define rm_sleep RM_CALL(LW_RM_SLOT_SLEEP, int (*)(DWORD))
#define rm_uptime_ms RM_CALL(LW_RM_SLOT_UPTIME_MS, DWORD (*)(void))

/* Copy a NUL-terminated caption (0..255 bytes). NULL/overlong strings fail
 * without changing the caption. Image setter: loaded, unbound images only;
 * window setter: only the live window's owning task. No caller pointer is kept.
 */
#define rm_image_set_caption RM_CALL(LW_RM_SLOT_IMAGE_SET_CAPTION, int (*)(PRM_IMAGE, PCSTR))
#define rm_window_set_caption RM_CALL(LW_RM_SLOT_WINDOW_SET_CAPTION, int (*)(DWORD, PCSTR))

#endif
