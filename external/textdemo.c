#include "stdio.h"
#include "console.h"
#include "crt.h"

/* CRT binds stdout once; printing never needs a window ID. */
int main(void) {
    const LW_ENV *env=lw_env;
    stdio_set_attr(0x0f);
    printf("HELLO FROM TEXTDEMO - printf -> GHOST window");
    stdio_set_attr(0x0a);
    for (DWORD row=1;row<24;row++) {
        stdio_set_cursor(0,row);putchar('|');
        stdio_set_cursor(79,row);putchar('|');
    }
    stdio_set_cursor(0,24);stdio_set_attr(0x0e);
    printf("80 columns x 25 lines. Type here; Enter returns 42. Click desktop: C closes.");
    stdio_set_cursor(0,3);stdio_set_attr(0x0f);
    for (;;) {
        LW_WINDOW_EVENT event;
        int rc;
        while ((rc=rm_window_poll(env->vi.window_id,&event))==1) {
            if (event.type==LW_WINDOW_FOCUS) {
                DWORD x,y;
                stdio_get_cursor(&x,&y);
                stdio_set_cursor(0,1);stdio_set_attr(0x0b);
                printf("%-10s",event.value ? "FOCUSED" : "UNFOCUSED");
                stdio_set_cursor(x,y);stdio_set_attr(0x0f);
            } else if (event.type==LW_WINDOW_KEY) {
                if (event.value==VK_F2) {
                    char caption[]="TEXTDEMO - custom caption";
                    if (rm_window_set_caption(env->vi.window_id,caption)!=RM_OK)return -6;
                    caption[0]='X';
                }
                if (event.value==VK_ENTER)return 42;
                if (event.value>=32 && event.value<127)printf("%c",event.value);
            }
        }
        if (rc<0)return -4;
        if (rm_window_wait(env->vi.window_id)!=RM_OK)return -5;
    }
}
