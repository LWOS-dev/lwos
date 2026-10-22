#include "gui.h"
#include "abi.h"
#include "convert.h"
#include "task.h"
#include "../../resource/vgafont.h"

#define DESKTOP_COLOR 0x182838u
#define CELL_W 8
#define CELL_H 16
#define BORDER 4
#define TITLE_H 20

static const DWORD palette[16]={
    0x000000,0x0000aa,0x00aa00,0x00aaaa,0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,
    0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff
};

int gui_open(GUI_DISPLAY *display) {
    if (!display)return RM_ERR_ARGUMENT;
    if (display->backbuffer)return RM_ERR_NOT_READY;
    DWORD width=lw_get_fb_w(), height=lw_get_fb_h(), pitch=lw_get_fb_pitch();
    PVOID framebuffer=(PVOID)lw_get_fb();
    if (!framebuffer || !width || !height || lw_get_fb_bpp()!=32 ||
        width>0x1fffffffu || pitch<width*4 || pitch>0x7ffff000u/height)
        return RM_ERR_NOT_READY;
    PVOID buffer=rm_alloc(pitch*height,4,0);
    if (!buffer)return RM_ERR_MEMORY;
    memzero(buffer,(int)(pitch*height));
    if (lw_gfx_enter()!=0) {
        rm_free(buffer);
        return RM_ERR_NOT_READY;
    }
    set_fb((PVOID)lw_get_fb());
    set_bb(buffer);
    set_info(width,height,32,pitch);
    set_vf_img(g_8x16_font);
    display->backbuffer=buffer;
    display->width=width;
    display->height=height;
    return RM_OK;
}

void gui_close(GUI_DISPLAY *display) {
    if (!display || !display->backbuffer)return;
    lw_gfx_exit();
    rm_free(display->backbuffer);
    set_bb(0);
    set_fb(0);
    set_info(0,0,32,0);
    memzero(display,sizeof(*display));
}

void gui_draw_desktop(PCSTR message) {
    gfx_fill(DESKTOP_COLOR);
    if (message)puts_xy(16,16,message,0xffffff,DESKTOP_COLOR);
}
void gui_draw_error(PCSTR message) {
    if (message)puts_xy(16,32,message,0xff5555,DESKTOP_COLOR);
}
RECT gui_text_window_rect(const GUI_TEXT_WINDOW *window) {
    return (RECT){window->x,window->y,
        LW_TEXT_COLS*CELL_W+2*BORDER,LW_TEXT_LINES*CELL_H+TITLE_H+2*BORDER};
}
int gui_text_window_contains(const GUI_TEXT_WINDOW *window, int x, int y) {
    RECT r=gui_text_window_rect(window);
    return (long long)x>=r.x && (long long)y>=r.y &&
        (long long)x<(long long)r.x+r.w && (long long)y<(long long)r.y+r.h;
}
void gui_draw_text_window(const GUI_TEXT_WINDOW *window,
                          const RM_TEXT_SNAPSHOT *snapshot, int focused) {
    RECT r=gui_text_window_rect(window);
    int client_x=r.x+BORDER, title_y=r.y+BORDER;
    int client_y=title_y+TITLE_H;
    DWORD title_color=focused ? 0x000080 : 0x555555;
    draw_rect(&r,0xaaaaaa);
    draw_rectr(client_x,title_y,LW_TEXT_COLS*CELL_W,TITLE_H,title_color);
    /* Keep long titles inside the title bar and leave room for exit status. */
    if (window->title) {
        int limit=snapshot->task_state==TASK_DEAD ? 64 : 79;
        for (int i=0;i<limit && window->title[i];i++)
            putc_xy(client_x+4+i*CELL_W,title_y+2,(BYTE)window->title[i],
                0xffffff,title_color);
    }
    if (snapshot->task_state==TASK_DEAD) {
        char code[9]={0};
        hex_xlat((DWORD)snapshot->exit_code,code,8);
        puts_xy(client_x+(LW_TEXT_COLS-14)*CELL_W,title_y+2,code,0xffffff,title_color);
    }
    for (DWORD y=0;y<LW_TEXT_LINES;y++)
        for (DWORD x=0;x<LW_TEXT_COLS;x++) {
            WORD cell=snapshot->cells[y*LW_TEXT_COLS+x];
            BYTE ch=(BYTE)cell;
            putc_xy(client_x+x*CELL_W,client_y+y*CELL_H,ch<128?ch:'?',
                palette[(cell>>8)&15],palette[(cell>>12)&15]);
        }
}
void gui_erase_text_window(const GUI_TEXT_WINDOW *window) {
    RECT r=gui_text_window_rect(window);
    draw_rect(&r,DESKTOP_COLOR);
}
RTPCS mouse_rect;
void gui_draw_pointer(int x, int y, int pressed, PGUI_DISPLAY disp) {
    //putpixel(x,y,pressed ? 0x00ff00 : 0xff0000);
    mouse_rect.info.x=x;
    mouse_rect.info.y=y;
    rectp_capture(&mouse_rect,disp);

    for (int i=0; i<8; i++) {
        putpixel(x+i+1,y+i+1,0xffffff);
        putpixel(x+7-i,y+i,0);
        putpixel(x+i,y,0);
        putpixel(x,y+i,0);
        if (i>6)continue;
        putpixel(x+i+3,y+i+2,0);
        putpixel(x+i+2,y+i+3,0);
        if (i>4)continue;
        putpixel(x+i+1,y+1,0xffffff);
        putpixel(x+1,y+i+1,0xffffff);
        if (i>2)continue;
        putpixel(x+i+3,y+2,0);
        putpixel(x+2,y+i+3,0);
    }
    putpixel(x+9,y+9,0);
}
void gui_present(PGUI_DISPLAY disp) {
    gfx_update();
    rectp_restore(&mouse_rect,disp);
}
void rectp_clear(PRTPCS p) {
    if (!p || !p->buffer)return;
    if (!p->info.h || !p->info.w)return;
    rm_free(p->buffer);
}
void rectp_alloc(PRTPCS p) {
    if (!p)return;
    if (p)rm_free(p->buffer);
    int size=p->info.w*p->info.w*sizeof(DWORD);
    p->buffer=rm_alloc(size,4,0);
    if (!p->buffer)return;
    memzero(p->buffer,size);
}
void rectp_capture(PRTPCS p, PGUI_DISPLAY f) {
    if (!f || !f->backbuffer)return;
    if (!p)return;
    if (!p->buffer)rectp_alloc(p);
    int x,y,w,h,tw,th;
    x=p->info.x;
    y=p->info.y;
    w=p->info.w;
    h=p->info.h;
    tw=f->width;
    th=f->height;
    if (x<0 || y<0 || w<0 || h<0)return;
    for (int xc=0; xc<w; xc++) {
        if ((x+xc)>(f->width))break;
        for (int yc=0; yc<h; yc++) {
            if ((y+yc)>(f->height))break;
            ((PDWORD)p->buffer)[xc+yc*w]=
            ((PDWORD)f->backbuffer)[xc+x+(yc+y)*f->width];
        }
    }
}
void rectp_restore(PRTPCS s, PGUI_DISPLAY f) {
    if (!f || !f->backbuffer)return;
    if (!s)return;
    if (!s->buffer)return;
    int x,y,w,h,tw,th;
    x=s->info.x;
    y=s->info.y;
    w=s->info.w;
    h=s->info.h;
    tw=f->width;
    th=f->height;
    if (x<0 || y<0 || w<0 || h<0)return;
    for (int xc=0; xc<w; xc++) {
        if ((x+xc)>(f->width))break;
        for (int yc=0; yc<h; yc++) {
            if ((y+yc)>(f->height))break;
            ((PDWORD)f->backbuffer)[xc+x+(yc+y)*f->width]=
            ((PDWORD)s->buffer)[xc+yc*w];
        }
    }
}