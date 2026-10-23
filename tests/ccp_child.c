/* Foreground probe: argument passing and keyboard input on the borrowed window. */
#include "stdio.h"
#include "crt.h"
#include "string.h"
int main(int argc,char **argv) {
    if (argc!=3 || strcmp(argv[1],"one") || strcmp(argv[2],"two"))return -8;
    printf("ARGS OK\n");
    for (;;) {
        LW_WINDOW_EVENT event;
        int rc=rm_window_poll(lw_env->vi.window_id,&event);
        if (rc<0)return -9;
        if (rc==1 && event.type==LW_WINDOW_KEY) {
            if (event.value!='z')return -10;
            printf("INPUT OK\n");
            return 42;
        }
        if (!rc && rm_window_wait(lw_env->vi.window_id)!=RM_OK)return -11;
    }
}
