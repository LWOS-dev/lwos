#ifndef _LW_GUI_H
#define _LW_GUI_H

#include "gfx.h"
#include "resman.h"

/* Single-display renderer on top of gfx. Caller initializes lw_abi_base and
 * rm_base, zero-initializes the display, then opens/closes it once. The gfx
 * backend is global: only one display may be active. Focus and task lifetime
 * remain the caller's responsibility. Draw operations require an open display.
 */
typedef struct _GUI_DISPLAY {
    PVOID backbuffer;
    DWORD width, height;
} GUI_DISPLAY, *PGUI_DISPLAY;

typedef struct _GUI_TEXT_WINDOW {
    int x, y;
    PCSTR title;
} GUI_TEXT_WINDOW;
extern RTPCS mouse_rect;

int gui_open(GUI_DISPLAY *display);
void gui_close(GUI_DISPLAY *display);
void gui_draw_desktop(PCSTR message);
void gui_draw_error(PCSTR message);
RECT gui_text_window_rect(const GUI_TEXT_WINDOW *window);
int gui_text_window_contains(const GUI_TEXT_WINDOW *window, int x, int y);
void gui_draw_text_window(const GUI_TEXT_WINDOW *window,
                          const RM_TEXT_SNAPSHOT *snapshot, int focused);
void gui_erase_text_window(const GUI_TEXT_WINDOW *window);
void gui_draw_pointer(int x, int y, int pressed, PGUI_DISPLAY f);
void gui_present(PGUI_DISPLAY disp);
void rectp_alloc(PRTPCS p);
void rectp_clear(PRTPCS p);

void rectp_capture(PRTPCS p, PGUI_DISPLAY f); // 从某个缓存截取一块选区
void rectp_restore(PRTPCS s, PGUI_DISPLAY f); // 从选区搬回缓存
void rectp_clear(PRTPCS p); // 禁用选区

#endif
