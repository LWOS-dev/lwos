#include "abi.h"
#include "app.h"
#include "task.h"
#include "net.h"

PVOID *lw_abi_base,*rm_base;
volatile DWORD stage,error,child_id;
static DWORD id,window;
static TASK *tasks;
static RM_TEXT_SNAPSHOT snapshot;
#define CHECK(c) do {if (!(c)) {error=__LINE__;goto done;}} while (0)
static int get(void) {
    for (unsigned slot=0;slot<32;slot++)
        if (!rm_text_snapshot(slot,&snapshot) && snapshot.task_id==id) {
            window=snapshot.window_id;return 1;
        }
    return 0;
}
static int prompt(void) {
    if (!get() || snapshot.task_state!=TASK_WAITING || snapshot.cursor%80!=8)return 0;
    const char *text="[/BIN]$ ";unsigned row=snapshot.cursor/80;
    for (unsigned i=0;i<8;i++)if ((BYTE)snapshot.cells[row*80+i]!=text[i])return 0;
    return 1;
}
static int receiver_waiting(void) {
    if (!get() || snapshot.task_state!=TASK_WAITING || snapshot.cursor%80)return 0;
    unsigned row=snapshot.cursor/80;
    if (!row)return 0;
    const char *text="netrecv: waiting for LWFT";
    for (unsigned i=0;text[i];i++)if ((BYTE)snapshot.cells[(row-1)*80+i]!=text[i])return 0;
    return 1;
}
static int command(const char *p) {
    while (*p) {
        LW_WINDOW_EVENT event={LW_WINDOW_KEY,*p++};
        if (rm_window_send(window,&event))return 0;
        rm_sleep(1);
    }
    LW_WINDOW_EVENT enter={LW_WINDOW_KEY,VK_ENTER};
    return rm_window_send(window,&enter)==0;
}
int lwp_main(PCLW_ENV env) {
    lw_abi_base=env->abi;rm_base=env->rm;
    tasks=(TASK*)rm_base[LW_RM_SLOT_TASK_TABLE];
    CHECK(rm_text_host(1)==0);
    const char *commands[]={"netrecv /RX.BIN","netrecv /EMPTY.BIN","netrecv /BAD.BIN",
                            "netrecv /KILL.BIN","netrecv /AGAIN.BIN"};
    for (unsigned i=0;i<5;i++) {
        if (!id) {
            CHECK(lw_app_launch(lw_blk_get(0),"/CCP.EXE",0,&id)==0);
            while (!prompt())rm_sleep(1);
        }
        CHECK(command(commands[i]));
        /* Enter has to be consumed before exposing the transfer to the host. */
        while (!receiver_waiting())rm_sleep(1);
        child_id=id;stage=i+1;
        if (i==3) {
            rm_sleep(500);
            CHECK(rm_task_kill(id)==0);
            int code;CHECK(rm_task_reap(id,&code)==0);id=0;
            CHECK(code==RM_TASK_EXIT_KILLED);
        } else {
            while (!prompt())rm_sleep(1);
        }
    }
    /* Kill and completed calls must release the endpoint. Check recv timeout. */
    BYTE mac[6],buffer[LW_NET_FRAME_MAX];
    CHECK(rm_net_open(mac)==0);
    DWORD before=rm_uptime_ms();
    CHECK(rm_net_recv(buffer,sizeof(buffer),20)==0);
    CHECK(rm_uptime_ms()-before>=20);
    CHECK(rm_net_close()==0);
    stage=6;
done:
    if (id) {int code;rm_task_kill(id);rm_task_reap(id,&code);}
    rm_text_host(0);
    return error ? -(int)error : 42;
}
