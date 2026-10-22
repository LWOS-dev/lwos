/* Disposable GUI-host probe: two independently relocated CRT/stdout copies. */
#include "abi.h"
#include "app.h"
#include "task.h"
PVOID *lw_abi_base;
PVOID *rm_base;
volatile DWORD stage, error;
static DWORD ids[3];
static RM_TEXT_SNAPSHOT snapshots[3];
#define CHECK(condition) do {if (!(condition)) {error=__LINE__;goto done;}} while (0)
static int get(unsigned index) {
    for (DWORD slot=0;slot<32;slot++)
        if (rm_text_snapshot(slot,&snapshots[index])==RM_OK && snapshots[index].task_id==ids[index])
            return 1;
    return 0;
}
static int wait_state(unsigned index, DWORD state) {
    for (unsigned i=0;i<2000;i++) {
        if (get(index) && snapshots[index].task_state==state)return 1;
        rm_sleep(1);
    }
    return 0;
}
static int text_is(unsigned index, unsigned row, const char *expected) {
    if (!get(index))return 0;
    for (unsigned i=0;expected[i];i++)
        if ((BYTE)snapshots[index].cells[row*80+i]!=(BYTE)expected[i])return 0;
    return 1;
}
static int send(unsigned index, int key) {
    LW_WINDOW_EVENT event={LW_WINDOW_KEY,key};
    return rm_window_send(snapshots[index].window_id,&event)==RM_OK;
}
static int wait_text(unsigned index, const char *expected) {
    for (unsigned i=0;i<2000;i++) {
        if (text_is(index,3,expected))return 1;
        rm_sleep(1);
    }
    return 0;
}
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    lw_abi_base=env->abi;rm_base=env->rm;
    CHECK(rm_text_host(1)==RM_OK);
    stage=1;
    CHECK(lw_app_launch(lw_blk_get(0),"/TEXTDEMO.EXE",0,&ids[0])==RM_OK);
    CHECK(lw_app_launch(lw_blk_get(0),"/TEXTDEMO.EXE",0,&ids[1])==RM_OK);
    CHECK(wait_state(0,TASK_WAITING) && wait_state(1,TASK_WAITING));
    CHECK(snapshots[0].window_id!=snapshots[1].window_id);
    CHECK(text_is(0,0,"HELLO FROM TEXTDEMO") && text_is(1,0,"HELLO FROM TEXTDEMO"));
    stage=2;
    CHECK(send(0,'a') && send(1,'Z') && send(0,'b') && send(0,'c'));
    CHECK(wait_text(0,"abc ") && wait_text(1,"Z "));
    CHECK(send(0,VK_ENTER) && wait_state(0,TASK_DEAD));
    CHECK(snapshots[0].exit_code==42);
    CHECK(send(1,'Y') && wait_text(1,"ZY ") && text_is(0,3,"abc "));
    CHECK(send(1,VK_ENTER) && wait_state(1,TASK_DEAD));
    CHECK(snapshots[1].exit_code==42);
    stage=3;
    CHECK(lw_app_launch(lw_blk_get(0),"/HELLO.EXE",0,&ids[2])==RM_OK);
    CHECK(wait_state(2,TASK_DEAD) && snapshots[2].exit_code==0);
    CHECK(text_is(2,0,"Hello, world!"));
    CHECK(text_is(2,1,"signed=-123 unsigned=123 hex=00001234"));
    CHECK(text_is(2,2,"Each program has its own stdout and cursor."));
    stage=4;
done:
    for (unsigned i=0;i<3;i++)if (ids[i]) {
        int code;
        rm_task_kill(ids[i]);
        rm_task_reap(ids[i],&code);
    }
    rm_text_host(0);
    return error ? -(int)error : 42;
}
