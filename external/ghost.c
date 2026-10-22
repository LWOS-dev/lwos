// gui host
#include "abi.h"
#include "resman.h"
#include "gui.h"
#include "app.h"
#include "task.h"

PVOID *lw_abi_base;
PVOID *rm_base;
static GUI_DISPLAY display;
static GUI_TEXT_WINDOW demo_window={16,48,0};
static DWORD demo_id;
static DWORD focus_window;
static RM_TEXT_SNAPSHOT snapshot;
/* Focus is a GHOST policy. There is no stacking order yet. */
static int update_window(void) {
    if (demo_id) {
        for (DWORD slot=0;slot<32;slot++) {
            if (rm_text_snapshot(slot,&snapshot)==RM_OK && snapshot.task_id==demo_id) {
                if (snapshot.task_state==TASK_DEAD)focus_window=0;
                return 1;
            }
        }
    }
    focus_window=0;
    return 0;
}
static void set_focus(DWORD window) {
    if (focus_window==window)return;
    LW_WINDOW_EVENT event={LW_WINDOW_FOCUS,0};
    if (focus_window)rm_window_send(focus_window,&event);
    focus_window=0;
    if (window) {
        event.value=1;
        if (rm_window_send(window,&event)==RM_OK)focus_window=window;
    }
}

static void close_demo(void) {
    if (!demo_id)return;
    set_focus(0);
    int code;
    rm_task_kill(demo_id); /* DEAD is already safe to reap. */
    if (rm_task_reap(demo_id,&code)==RM_OK)demo_id=0;
}
static void draw_demo(void) {
    if (!update_window())return;
    demo_window.caption=snapshot.caption;
    gui_draw_text_window(&demo_window,&snapshot,focus_window==snapshot.window_id);
}

__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version != LW_ENV_VERSION ||
        env->size < sizeof(*env) || !env->abi)
        return -1;
    lw_abi_base = env->abi;
    rm_base=env->rm;
    int rc=gui_open(&display);
    if (rc!=RM_OK)return rc;
    if (rm_text_host(1)!=RM_OK) {
        gui_close(&display);
        return RM_ERR_NOT_READY;
    }
    gui_draw_desktop_c();
    RM_MOUSE_POS pos = {(int)display.width/2, (int)display.height/2};
    mouse_rect.info.x=0,mouse_rect.info.y=0;
    mouse_rect.info.w=10,mouse_rect.info.h=10;
    rectp_alloc(&mouse_rect);
    int running=1;
    int pointer_pressed=0;
    while (running) {
        int key;
        update_window();
        while ((key=rm_getmpoll())!=VK_NONE) {
            if (key==VK_LBUTTON) {
                rm_getmpos(&pos);
                int inside=update_window() && snapshot.task_state!=TASK_DEAD &&
                    gui_text_window_contains(&demo_window,pos.x,pos.y);
                set_focus(inside ? snapshot.window_id : 0);
            }
            if (key>=0 && key<VK_LBUTTON && focus_window) {
                LW_WINDOW_EVENT event={LW_WINDOW_KEY,key};
                int rc=rm_window_send(focus_window,&event);
                if (rc)gui_draw_error("Window input unavailable");
                continue;
            }
            if (key==VK_ESC) running=0;
            if (key=='t' || key=='T') {
                int rc=demo_id ? RM_ERR_NOT_READY :
                    lw_app_launch(lw_blk_get(0),"/CCP.EXE",0,&demo_id);
                if (rc)gui_draw_error("TEXTDEMO launch failed");
                else if (update_window() && snapshot.task_state!=TASK_DEAD)
                    set_focus(snapshot.window_id);
            }
            if (key=='c' || key=='C') {
                close_demo();
                gui_erase_text_window(&demo_window);
            }
            if (key==VK_LBUTTON) pointer_pressed=1;
            if (key==(VK_LBUTTON|VK_KEYUP)) pointer_pressed=0;
        }
        rm_getmpos(&pos);
        draw_demo();
        gui_draw_pointer(pos.x,pos.y,pointer_pressed,&display);
        gui_present(&display);
        if (running) {
            if (demo_id)asm volatile("sti; hlt":::"memory");
            else rm_input_wait();
        }
    }

    close_demo();
    rm_text_host(0);
    gui_close(&display);
    return RM_OK;
}
