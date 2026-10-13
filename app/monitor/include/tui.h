#ifndef _LW_MON_TUI_H
#define _LW_MON_TUI_H

#include "stdint.h"

#define TUI_BLACK   0
#define TUI_BLUE    1
#define TUI_GREEN   2
#define TUI_AQUA    3
#define TUI_RED     4
#define TUI_PURPLE  5
#define TUI_YELLOW  6
#define TUI_WHITE   7
#define TUI_LIGHT   8

typedef struct _TUI_AREA {
    DWORD x, y;
    DWORD w, h;
    DWORD index;
    PCSTR name;
} TUI_AREA, *PTUI_AREA;

extern PWORD video;
extern BYTE vi_attr;
extern DWORD focus; 

void draw_area(PTUI_AREA p);

void putxy(const char* str, int x, int y);
void putxyn(const char* str, int x, int y, int n);
void putxyc(BYTE c, int x, int y);
void tui_init(void);
void tui_loop(void);

#endif