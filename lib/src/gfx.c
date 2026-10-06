#include "gfx.h"

DWORD fb_w, fb_h, pitch;
PDWORD fb;
PDWORD bb;

RECT dirty_rects[16];
int dirty_rect_cnt;

static void add_dr(PCRECT pr) {
    dirty_rects[dirty_rect_cnt].x=pr->x;
    dirty_rects[dirty_rect_cnt].y=pr->y;
    dirty_rects[dirty_rect_cnt].w=pr->w;
    dirty_rects[dirty_rect_cnt].h=pr->h;
    dirty_rect_cnt++;
}

void set_info(int _w, int _h, int _bpp, int _pitch) {
    fb_w=_w;
    fb_h=_h;
    pitch=_pitch;
}
void set_fb(PVOID ptr) {
    fb=ptr;
}
void set_bb(PVOID ptr) {
    bb=ptr;
}
void putpixel(int x, int y, DWORD color) {
    if (x<0 || y<0 || (DWORD)x>=fb_w || (DWORD)y>=fb_h)return;
    ((PDWORD)((PBYTE)bb+(DWORD)y*pitch))[x]=color;
}
int fb_size() {
    return pitch*fb_h;
}
void gfx_clear() {
    memzero(bb, fb_size());
}
void gfx_update() {
    for (int i=0; i<fb_size()/sizeof(int); i++) {
        fb[i]=bb[i];
    }
}
void gfx_update_rect(PRECT p) {
    for (int i=p->x; i<p->x+p->w; i++) {
        for (int j=p->y; j<p->y+p->h; j++) {
            ((PDWORD)((PBYTE)fb+(DWORD)j*pitch))[i]=
            ((PDWORD)((PBYTE)bb+(DWORD)j*pitch))[i];
        }
    }
}
void gfx_update_region() {
    for (int i=0; i<dirty_rect_cnt; i++) {
        gfx_update_rect(&dirty_rects[i]);
    }
    dirty_rect_cnt=0;
}
void gfx_fill(DWORD color) {
    for (int i=0; i<fb_size(); i++) {
        bb[i]=color;
        fb[i]=color;
    }
}

PBYTE img_vgafont;

void set_vf_img(PVOID ptr) {
    img_vgafont=ptr;
}
void putc_xy(int x, int y, BYTE idx, DWORD fc, DWORD bc) {
    for (int i=0; i<16; i++) {
        for (int j=0; j<8; j++) {
            if (img_vgafont[idx*16+i]&(0x80>>j)) {
                putpixel(x+j,y+i,fc);
            } else {
                putpixel(x+j,y+i,bc);
            }
        }
    }
}
void puts_xy(int x, int y, PCSTR str, DWORD fc, DWORD bc) {
    int i=0;
    PCSTR p=str;
    RECT d;
    d.x=x;
    d.y=y;
    d.h=16;
    while (*p) {
        putc_xy(x+i*8,y,*p,fc,bc);
        i++;
        p++;
    }
    d.w=8*i;
    add_dr(&d);
}
void draw_rect(int x, int y, int w, int h, DWORD color) {
    RECT d;
    d.x=x;
    d.y=y;
    d.w=w;
    d.h=h;
    add_dr(&d);
    for (int i=x; i<x+w; i++) {
        for (int j=y; j<y+h; j++) {
            putpixel(i,j,color);
        }
    }
}

int is_inside(int x, int y, int x1, int y1, int x2, int y2) {
    if (x>=x1 || x<x2) {
        if (y>=y1 || y<y2) {
            return 1;
        }
    }
    return 0;
}