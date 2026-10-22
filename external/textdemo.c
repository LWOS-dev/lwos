#include "resman.h"

PVOID *rm_base;

/* Text output uses the loader buffer; input arrives through window events. */
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env)
{
    if (!env || env->version!=LW_ENV_VERSION || env->size<sizeof(*env) ||
        env->vi.size<sizeof(LW_VI) || env->vi.mode!=LW_VI_TEXT || !env->vi.txt)
        return -1;
    rm_base=env->rm;
    if (!rm_base)return -3;
    const LW_BUF_TXT *txt=env->vi.txt;
    if (!txt->buffer || txt->cols!=80 || txt->lines!=25)return -2;
    
    volatile WORD *cells=(volatile WORD *)txt->buffer;
    const char *message="HELLO FROM TEXTDEMO - loader buffer -> GHOST window";
    for (DWORD i=0;message[i];i++)cells[i]=0x0f00|(BYTE)message[i];
    for (DWORD row=1;row<25;row++) {
        cells[row*80]=0x0a00|'|';
        cells[row*80+79]=0x0a00|'|';
    }
    const char *last="80 columns x 25 lines. Type here; Enter returns 42. Click desktop: C closes.";
    for (DWORD i=0;last[i];i++)cells[24*80+i]=0x0e00|(BYTE)last[i];
    DWORD cursor=3*80;
    for (;;) {
        LW_WINDOW_EVENT event;
        int rc;
        while ((rc=rm_window_poll(env->vi.window_id,&event))==1) {
            if (event.type==LW_WINDOW_FOCUS) {
                const char *status=event.value ? "FOCUSED   " : "UNFOCUSED ";
                for (DWORD i=0;status[i];i++)cells[80+i]=0x0b00|(BYTE)status[i];
            } else if (event.type==LW_WINDOW_KEY) {
                if (event.value==VK_ENTER)return 42;
                if (event.value>=32 && event.value<127) {
                    cells[cursor++]=0x0f00|(BYTE)event.value;
                    if (cursor>=24*80)cursor=3*80;
                }
            }
        }
        if (rc<0)return -4;
        if (rm_window_wait(env->vi.window_id)!=RM_OK)return -5;
    }
}
