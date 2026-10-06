#include "../include/resman.h"
#include "abi.h"

#include "mem.h"
#include "gfx.h"
#include "mouse.h"
#include "ui.h"

PVOID *lw_abi_base;
PVOID p;

/* The scene stays in the backbuffer; only the small cursor overlay touches VRAM. */
static void cursor_present(int x, int y, BYTE buttons, int erase)
{
    int w = lw_get_fb_w(), h = lw_get_fb_h();
    DWORD stride = lw_get_fb_pitch();
    PBYTE front = (PBYTE)lw_get_fb();
    /* draw_mouse shape: two 5-pixel arms and a 10-pixel diagonal.
     * Draw directly to the front buffer so the saved scene stays cursor-free.
     */
    for (int row = 0; row < 10 && y + row < h; row++) {
        PDWORD dst = (PDWORD)(front + (y + row) * stride);
        PDWORD src = (PDWORD)((PBYTE)p + (y + row) * stride);
        for (int col = 0; col < 10 && x + col < w; col++) {
            if (erase) dst[x + col] = src[x + col];
            else if ((row == 0 && col < 5) ||
                     (col == 0 && row < 5) || col == row)
                dst[x + col] = buttons ? 0xff4040 : 0;
        }
    }
}
static void draw_cursor(int x,int y,BYTE buttons) {
    for (int i=0;i<5;i++) {
        putpixel(x,y+i,0xffffff);
        putpixel(x+i,y,0xffffff);
    }
    for (int i=0;i<10;i++) {
        putpixel(x+i,y+i,0xffffff);
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
    gui->x += event->dx;
    gui->y -= event->dy;
    if (gui->x < 0) gui->x = 0;
    if (gui->y < 0) gui->y = 0;
    if ((DWORD)gui->x >= lw_get_fb_w()) gui->x = lw_get_fb_w() - 1;
    if ((DWORD)gui->y >= lw_get_fb_h()) gui->y = lw_get_fb_h() - 1;
    gui->buttons = event->buttons;
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
DWORD cur_temp[100];
static void present_cursor(GUI_STATE *gui)
{
    if (!gui->cursor_dirty) return;
    cursor_present(gui->drawn_x, gui->drawn_y, 0, 1);
    cursor_present(gui->x, gui->y, gui->buttons, 0);
    gui->drawn_x = gui->x;
    gui->drawn_y = gui->y;

    gui->cursor_dirty = 0;
}

DRAG drag;

static void test_gfx(GUI_STATE *gui) {
    puts_xy(50, 50, "HELLO\0", 0, 0xffffff);
}

static void resman_loop(void)
{
    GUI_STATE gui = {0};
    gui.running = 1;
    gui.x = gui.drawn_x = lw_get_fb_w() / 2;
    gui.y = gui.drawn_y = lw_get_fb_h() / 2;
    gui.cursor_dirty = 1;
    while (gui.running) {
        poll_input(&gui);
        test_gfx(&gui);
        if ();
        /* Add application updates here, keeping each iteration nonblocking. */
        gfx_update_region();
        present_cursor(&gui);
    }
}

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

    drag.X=50,drag.Y=50,drag.W=100,drag.H=100;

    resman_loop();
    mouse_shutdown();
    lw_gfx_exit();
    return RM_OK;
}

DWORD resman_version(void)
{
    return 1;
}
