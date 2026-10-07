#ifndef _UI_H
#define _UI_H

#include "gfx.h"

typedef struct _BUTTON {
    RECT R;
    PCHAR NAME;
    DWORD STATUS;
} BUTTON, *PBUTTON;

void draw_button(PBUTTON p);

typedef struct _DRAG {
    RECT R;
    int dragging;
    int offset_x, offset_y;
} DRAG, *PDRAG;

void draw_drag(PDRAG p);
/* Returns 1 when the rectangle moves. Capture starts on a left-button edge. */
int drag_mouse(PDRAG p, int x, int y, BYTE previous, BYTE buttons);

#endif
