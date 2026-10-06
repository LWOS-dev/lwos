#ifndef _LW_MOUSE_H
#define _LW_MOUSE_H
#include "stdint.h"
#define MOUSE_LEFT 1u
#define MOUSE_RIGHT 2u
#define MOUSE_MIDDLE 4u
typedef struct _MOUSE_EVENT {
    int dx, dy; /* PS/2 coordinates: positive dy points up */
    BYTE buttons;
} MOUSE_EVENT;
/* Polling PS/2 driver; init returns 0 on success, -1 on failure.
 * Initialize after monitor enables the keyboard, with keyboard polling paused.
 */
int mouse_init(void);
void mouse_shutdown(void);
void mouse_poll(void); /* bounded, leaves keyboard bytes unread */
int mouse_read(MOUSE_EVENT *event); /* 1 = event, 0 = empty */
void mouse_feed(BYTE data); /* decoder only; not for command replies */
#endif
