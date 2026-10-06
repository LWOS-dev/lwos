#ifndef _UI_H
#define _UI_H

#include "gfx.h"

typedef struct _BUTTON {
    DWORD X, Y;
    DWORD W, H;
    PCHAR NAME;
    DWORD STATUS;
} BUTTON, *PBUTTON;

void draw_button(PBUTTON p);

typedef struct _DRAG {
    DWORD X, Y;
    DWORD W, H;
} DRAG, *PDRAG;

void draw_drag(PDRAG p);

#endif