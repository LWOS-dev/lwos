// gui host
#include "abi.h"
#include "resman.h"
#include "gfx.h"
#include "string.h"
#include "convert.h"
#include "mem.h"

#include "tui/tui.h"

PVOID *lw_abi_base;
PVOID *rm_base;
PVOID backbuffer,framebuffer;
char text[100];

__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version != LW_ENV_VERSION ||
        env->size < sizeof(*env) || !env->abi)
        return -1;
    lw_abi_base = env->abi;
    rm_base=env->rm;

    DWORD width=lw_get_fb_w(), height=lw_get_fb_h();
    DWORD pitch=lw_get_fb_pitch();
    if (!width || !height || lw_get_fb_bpp()!=32 ||
        width>0x1fffffffu || pitch<width*4 ||
        pitch>0x7ffff000u/height)
        return RM_ERR_NOT_READY;
    DWORD buffer_size=pitch*height; // k right use actual buffer size
    backbuffer=rm_alloc(buffer_size,4,0);
    framebuffer=(PVOID)lw_get_fb();
    memzero(text,100);
    if (!backbuffer) {
        putxy("FAILED ALLOCATE BACKBUFFER",0,0);
        return RM_ERR_MEMORY;
    }
    memzero(backbuffer,(int)buffer_size);
    memcpy(text,"ALLOCATED BACKBUFFER AT ",24);
    hex_xlat((DWORD)backbuffer,text+24,8);
    putxy(text,0,0);
    memzero(text,100);
    memcpy(text,"ALLOCATED FRAMEBUFFER AT ",25);
    hex_xlat((DWORD)framebuffer,text+25,8);
    putxy(text,0,1);
    if (lw_gfx_enter()!=0) {
        rm_free(backbuffer);
        return RM_ERR_NOT_READY;
    }
    set_fb((PVOID)lw_get_fb());
    set_bb(backbuffer);
    set_info(width,height,32,pitch);

    RM_MOUSE_POS pos = {(int)width/2, (int)height/2};
    int running=1;
    DWORD color=0xff0000;
    while (running) {
        int key;
        while ((key=rm_getmpoll())!=VK_NONE) {
            if (key==VK_ESC) running=0;
            if (key==VK_LBUTTON) color=0x00ff00;
            if (key==(VK_LBUTTON|VK_KEYUP)) color=0xff0000;
        }
        rm_getmpos(&pos);
        putpixel(pos.x,pos.y,color);
        gfx_update();
        if (running) rm_input_wait();
    }

    lw_gfx_exit();
    rm_free(backbuffer);
    return RM_OK;
}
