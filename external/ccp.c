#include "stdio.h"
#include "console.h"
#include "crt.h"
#include "convert.h"
#include "string.h"

CHAR prompt[256];
CHAR pwd[256];
PCHAR pmt_p;
/* 返回字符或 VK_*；失败返回 EOF。不回显。 */
static int console_readkey(void) {
    LW_WINDOW_EVENT event;
    DWORD id = lw_env->vi.window_id;
    for (;;) {
        int rc=rm_window_poll(id, &event);
        if (rc<0)return EOF;
        if (rc==1) {
            if (event.type==LW_WINDOW_KEY)return event.value;
            continue; /* 跳过焦点通知 */
        }
        if (rm_window_wait(id)!=RM_OK)
            return EOF;
    }
}
static void readline(void) {
    PSTR p=prompt;
    for (;;) {
        DWORD t;
        t=console_readkey();
        switch (t) {
            case EOF:return;
            case VK_ENTER:goto done;
            case VK_BKSPACE:{printf("\b \b");*p=0;p--;break;}
            default:{*(p++)=(CHAR)t;putchar(t);break;}
        }
    }
done:
    *p=0;
    printf("\n");
}
int dir_list() {
    return -1;
}
DWORD errno=0;
DWORD execute(PSTR p) {
    if (!p)return -1;
    if (!strcmp(p,"pwd")) { printf("%s\n",pwd); return 0; }
    if (!strcmp(p,"ls")) { return dir_list(); }
    if (!strcmp(p,"echo")) { printf("%s\n",pmt_p); return 0; }
    return -1;
}
int main(void) {
    //const LW_ENV *env=lw_env;
    strcpy(pwd,"/");
    stdio_set_attr(0x0f);
    printf("LWOS CCP v1.0 - Meow Technology\n");
    for (;;) {
        printf(" $ ");
        readline();
        pmt_p=strtok(prompt,' ');
        execute(prompt);
    }
    return 0;
}
