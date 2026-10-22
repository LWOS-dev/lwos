#include "stdio.h"
#include "console.h"
#include "crt.h"

/* CRT binds stdout once; printing never needs a window ID. */
int main(void) {
    const LW_ENV *env=lw_env;
    printf("LWOS CCP v1.0 - Meow Technology\nqwe");
    for (;;) {
        LW_WINDOW_EVENT event;
        int rc;
        while ((rc=rm_window_poll(env->vi.window_id,&event))==1) {
            ;
        }
        if (rc<0)return -4;
        if (rm_window_wait(env->vi.window_id)!=RM_OK)return -5;
    }
}
