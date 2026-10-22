#ifndef _LW_WINDOW_H
#define _LW_WINDOW_H

#include "stdint.h"
#include "keys.h"

enum { LW_WINDOW_KEY = 1, LW_WINDOW_FOCUS };
typedef struct _LW_WINDOW_EVENT {
    DWORD type;
    int value; /* KEY: decoded VK/character; FOCUS: 0 lost, 1 gained. */
} LW_WINDOW_EVENT;
#define LW_WINDOW_QUEUE_SIZE 32u
/* Includes the terminating NUL. Longer captions are rejected. */
#define LW_WINDOW_CAPTION_SIZE 256u

#endif
