#ifndef _GRAPHICS_H
#define _GRAPHICS_H

#include "mem.h"

typedef struct _GUI_STATE {
    int running;
    int x, y, drawn_x, drawn_y;
    BYTE buttons;
    int cursor_dirty;
    int scene_dirty;
    BYTE key_extended, key_skip;
} GUI_STATE;

typedef struct _RECT {
    int x,y,w,h;
} RECT, *PRECT;
typedef const RECT* PCRECT;

void set_info(int w, int h, int bpp, int pitch);
void set_fb(PVOID ptr);
void set_bb(PVOID ptr);
void putpixel(int x, int y, DWORD color);
int fb_size(void);
void gfx_clear(void);
void gfx_update_rect(PRECT p);
void gfx_update_region(void);
void gfx_invalidate_rect(PCRECT r);
void gfx_update(void);
void gfx_fill(DWORD color);

void set_vf_img(PVOID ptr);
void putc_xy(int x, int y, BYTE idx, DWORD fc, DWORD bc);
void puts_xy(int x, int y, PCSTR str, DWORD fc, DWORD bc);
void draw_rectr(int x, int y, int w, int h, DWORD color);
void draw_rect(PRECT r, DWORD color);

int is_inside(int x, int y, int x1, int y1, int x2, int y2);

#endif
