#include "../include/resman.h"
#include "abi.h"

#include "mem.h"
#include "gfx.h"
#include "mouse.h"
#include "ui.h"

#include "task.h"

PVOID *lw_abi_base;
PVOID p;
DRAG drag;
static DWORD cur_temp[100];
static void draw_cursor(int x, int y, BYTE buttons);

/* Compose in RAM, present once, then restore the cursor-free backbuffer. */
static void cursor_present(int x, int y, BYTE buttons, int erase)
{
    int w = lw_get_fb_w(), h = lw_get_fb_h();
    DWORD stride = lw_get_fb_pitch();
    for (int row = 0; row < 10 && y + row < h; row++) {
        PDWORD pixels = (PDWORD)((PBYTE)p + (y + row) * stride);
        for (int col = 0; col < 10 && x + col < w; col++) {
            if (erase) pixels[x+col] = cur_temp[row*10+col];
            else cur_temp[row*10+col] = pixels[x+col];
        }
    }
    if (!erase) draw_cursor(x,y,buttons);
}
static void draw_cursor(int x,int y,BYTE buttons) {
    DWORD color=buttons?0xff4040:0;
    for (int i=0;i<5;i++) {
        putpixel(x,y+i,color);
        putpixel(x+i,y,color);
    }
    for (int i=0;i<10;i++) {
        putpixel(x+i,y+i,color);
    }
}

/* Set-1 key events, not ASCII: extend this handler for keyboard actions.
 * down=1: press/repeat; down=0: release; extended=1: E0-prefixed key.
 */
static void on_key(GUI_STATE *gui, BYTE code, int down, int extended)
{
    if (down && !extended && code == 0x01)
        gui->running = 0; /* Escape */
}

static void keyboard_feed(GUI_STATE *gui, BYTE scan)
{
    if (gui->key_skip) {
        gui->key_skip--;
        return;
    }
    /* Pause has a six-byte sequence; leave its decoding for later. */
    if (scan == 0xe1) {
        gui->key_skip = 5;
        gui->key_extended = 0;
        return;
    }
    if (scan == 0xe0) {
        gui->key_extended = 1;
        return;
    }
    on_key(gui, scan & 0x7f, !(scan & 0x80), gui->key_extended);
    gui->key_extended = 0;
}

static void on_mouse(GUI_STATE *gui, const MOUSE_EVENT *event)
{
    BYTE previous = gui->buttons;
    gui->x += event->dx;
    gui->y -= event->dy;
    if (gui->x < 0) gui->x = 0;
    if (gui->y < 0) gui->y = 0;
    if ((DWORD)gui->x >= lw_get_fb_w()) gui->x = lw_get_fb_w() - 1;
    if ((DWORD)gui->y >= lw_get_fb_h()) gui->y = lw_get_fb_h() - 1;
    gui->buttons = event->buttons;
    if (drag_mouse(&drag,gui->x,gui->y,previous,gui->buttons)) {
        int max_x=(int)lw_get_fb_w()-drag.R.w;
        int max_y=(int)lw_get_fb_h()-drag.R.h;
        if (max_x<0)max_x=0;
        if (max_y<0)max_y=0;
        if (drag.R.x<0)drag.R.x=0;
        if (drag.R.y<0)drag.R.y=0;
        if (drag.R.x>max_x)drag.R.x=max_x;
        if (drag.R.y>max_y)drag.R.y=max_y;
        gui->scene_dirty=1;
    }
    gui->cursor_dirty = 1;
}

/* Both devices share 0x60. Route by AUX; never block waiting for a key.
 * Do not call lw_getc/getk/getp here: keyboard LED commands would compete
 * with the mouse for controller replies. Input handlers consume events only.
 */
static void poll_input(GUI_STATE *gui)
{
    for (int i = 0; i < 32 && gui->running; i++) {
        MOUSE_EVENT event;
        mouse_poll();
        while (mouse_read(&event)) on_mouse(gui, &event);

        BYTE status = lw_inb(0x64);
        if (!(status & 0x01)) break;
        if (status & 0x20) continue; /* mouse_poll reads this next time */
        BYTE scan = lw_inb(0x60);
        if (status & 0xc0) {
            gui->key_extended = gui->key_skip = 0;
            continue;
        }
        keyboard_feed(gui, scan);
    }
}

static void test_gfx(GUI_STATE *gui) {
    puts_xy(50, 50, "HELLO\0", 0, 0xffffff);
}
static void present_cursor(GUI_STATE *gui)
{
    if (!gui->cursor_dirty && !gui->scene_dirty) return;
    if (gui->scene_dirty) {
        gfx_fill(0xffffff);
        draw_drag(&drag);
        test_gfx(gui);
    }
    RECT old={gui->drawn_x,gui->drawn_y,10,10};
    RECT next={gui->x,gui->y,10,10};
    gfx_invalidate_rect(&old);
    gfx_invalidate_rect(&next);
    cursor_present(gui->x, gui->y, gui->buttons, 0);
    gfx_update_region();
    cursor_present(gui->x, gui->y, gui->buttons, 1);
    gui->drawn_x = gui->x;
    gui->drawn_y = gui->y;

    gui->cursor_dirty = 0;
    gui->scene_dirty = 0;
}

static void resman_loop(void)
{
    GUI_STATE gui = {0};
    gui.running = 1;
    gui.x = gui.drawn_x = lw_get_fb_w() / 2;
    gui.y = gui.drawn_y = lw_get_fb_h() / 2;
    gui.cursor_dirty = 1;
    gui.scene_dirty = 1;
    while (gui.running) {
        poll_input(&gui);
        /* Add application updates here, keeping each iteration nonblocking. */
        present_cursor(&gui);
    }
}

SMP_SEG smp;

int resman_main(PVOID *abi)
{
    if (!abi || abi[LW_SLOT_MAGIC] != (PVOID)LW_ABI_MAGIC)
        return RM_ERR_ABI;

    lw_abi_base = abi;
    lw_puts("RESMAN STARTED AT 00180000H\n\r");

    static int memory_ready;
    if (!memory_ready) {
        mmap_init();
        mem_pool_init();
        memory_ready = 1;
    }

    gdt_init();

    seg_xlat(&smp, sys_gdt+5);
    lw_put_qword(*(PQWORD)(sys_gdt+5));
    lw_puts("\n\rBASE=");
    lw_put_dword(smp.base);
    lw_puts(" LIMIT=");
    lw_put_dword(smp.base+(smp.limit+1)<<12);

    while (1);

    return RM_OK;
    //暂时不用GUI

    if (lw_get_fb_bpp() != 32 || !lw_get_fb_w() || !lw_get_fb_h())
        return RM_ERR_FORMAT;
    set_info(lw_get_fb_w(), lw_get_fb_h(), 0, lw_get_fb_pitch());
    if (!p) {
        PVOID buffer = kmalloc(fb_size());
        if (!buffer || buffer == (PVOID)-1 || buffer == (PVOID)-2)
            return RM_ERR_MEMORY;
        p = buffer;
    }
    set_bb(p);
    set_vf_img((PVOID)0x350000);
    if (mouse_init() < 0) {
        lw_puts("PS/2 MOUSE INIT FAILED\n\r");
        return RM_ERR_NOT_READY;
    }
    if (lw_gfx_enter() != 0) {
        mouse_shutdown();
        return RM_ERR_NOT_READY;
    }
    set_fb((PVOID)lw_get_fb());
    gfx_clear();
    gfx_fill(0xffffff);

    drag.R.x=50,drag.R.y=50,drag.R.w=100,drag.R.h=100;
    drag.dragging=0;

    resman_loop();
    mouse_shutdown();
    lw_gfx_exit();
    return RM_OK;
}

DWORD resman_version(void)
{
    return 1;
}
