#ifndef _LW_RM_TUI_H
#define _LW_RM_TUI_H

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

extern PWORD video;
extern BYTE vi_attr;

void screen_fill(BYTE back);
void set_attr(BYTE fore, BYTE back);
void putxy(const char *str, int x, int y);
void putxyn(const char *str, int x, int y, int n);
void putxyc(BYTE c, int x, int y);

#endif
