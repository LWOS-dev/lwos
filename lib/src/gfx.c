#include "gfx.h"

DWORD fb_w, fb_h, pitch;
PDWORD fb;
PDWORD bb;

RECT dirty_rects[16];
int dirty_rect_cnt;

static void add_dr(PCRECT pr) {
    if (pr->w<=0 || pr->h<=0)return;
    if (dirty_rect_cnt>=16) {
        dirty_rect_cnt=1;
        dirty_rects[0]=(RECT){0,0,(int)fb_w,(int)fb_h};
        return;
    }
    dirty_rects[dirty_rect_cnt].x=pr->x;
    dirty_rects[dirty_rect_cnt].y=pr->y;
    dirty_rects[dirty_rect_cnt].w=pr->w;
    dirty_rects[dirty_rect_cnt].h=pr->h;
    dirty_rect_cnt++;
}

void gfx_invalidate_rect(PCRECT r) {
    add_dr(r);
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
    if (p->w<=0 || p->h<=0)return;
    int x=p->x<0?0:p->x, y=p->y<0?0:p->y;
    long long right=(long long)p->x+p->w, bottom=(long long)p->y+p->h;
    int x_end=right>(long long)fb_w?(int)fb_w:(int)right;
    int y_end=bottom>(long long)fb_h?(int)fb_h:(int)bottom;
    for (int i=x; i<x_end; i++) {
        for (int j=y; j<y_end; j++) {
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
    for (DWORD i=0; i<(DWORD)fb_size()/sizeof(DWORD); i++) {
        bb[i]=color;
    }
    dirty_rect_cnt=0;
    RECT full={0,0,(int)fb_w,(int)fb_h};
    add_dr(&full);
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
void draw_rectr(int x, int y, int w, int h, DWORD color) {
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
void draw_rect(PRECT r, DWORD color) {
    add_dr(r);
    for (int i=r->x; i<r->x+r->w; i++) {
        for (int j=r->y; j<r->y+r->h; j++) {
            putpixel(i,j,color);
        }
    }
}

int is_inside(int x, int y, int x1, int y1, int x2, int y2) {
    return x>=x1 && x<x2 && y>=y1 && y<y2;
}
