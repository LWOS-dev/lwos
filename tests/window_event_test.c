/* Host regression: window IDs, producer/consumer permissions and FIFO bounds. */
#include <assert.h>
#include "../app/resman/include/resman.h"
#include "../app/resman/include/task.h"
#include "mem.h"
extern RM_IMAGE images[RM_MAX_IMAGES];
TASK task[TASK_MAX];
PTASK current_task;
static int wakes;
PVOID memcpy(PVOID dest, PCVOID src, int count) {
    for (int i=0;i<count;i++)((PBYTE)dest)[i]=((PCBYTE)src)[i];
    return dest;
}
void task_wake_locked(DWORD id, DWORD reason) {
    assert(id==2 && reason==TASK_WAIT_WINDOW); wakes++;
}
void task_block_locked(DWORD reason) { (void)reason; assert(0); }
int main(void) {
    static WORD cells[LW_TEXT_COLS*LW_TEXT_LINES];
    task[1].id=1; task[1].state=TASK_RUNNING;
    task[2].id=2; task[2].state=TASK_WAITING;
    current_task=&task[1];
    assert(resman_text_host(1)==RM_OK);
    PRM_IMAGE img=&images[0];
    img->state=RM_IMAGE_BOUND; img->window_id=71;
    img->text_task=2; img->text.buffer=cells;
    LW_WINDOW_EVENT in={LW_WINDOW_KEY,'a'}, out;
    assert(resman_window_send(70,&in)==RM_ERR_NOT_READY);
    assert(resman_window_poll(71,&out)==RM_ERR_NOT_READY);
    assert(resman_window_send(71,0)==RM_ERR_ARGUMENT);
    in.type=99; assert(resman_window_send(71,&in)==RM_ERR_ARGUMENT);
    in.type=LW_WINDOW_FOCUS; in.value=2;
    assert(resman_window_send(71,&in)==RM_ERR_ARGUMENT);
    in.type=LW_WINDOW_KEY; in.value=VK_LBUTTON;
    assert(resman_window_send(71,&in)==RM_ERR_ARGUMENT);
    in.value=VK_NONE; assert(resman_window_send(71,&in)==RM_ERR_ARGUMENT);
    in.value='a'; current_task=&task[2];
    assert(resman_window_send(71,&in)==RM_ERR_NOT_READY);
    assert(resman_window_poll(71,&out)==0);
    for (unsigned cycle=0;cycle<3;cycle++) {
        current_task=&task[1];
        for (unsigned i=0;i<LW_WINDOW_QUEUE_SIZE-1;i++) {
            in.value=(int)i; assert(resman_window_send(71,&in)==RM_OK);
        }
        assert(resman_window_send(71,&in)==RM_ERR_NOT_READY);
        current_task=&task[2];
        for (unsigned i=0;i<LW_WINDOW_QUEUE_SIZE-1;i++) {
            assert(resman_window_poll(71,&out)==1);
            assert(out.type==LW_WINDOW_KEY && out.value==(int)i);
        }
        assert(resman_window_poll(71,&out)==0);
    }
    assert(wakes==3*(LW_WINDOW_QUEUE_SIZE-1));
    current_task=&task[1]; task[2].state=TASK_DEAD;
    assert(resman_window_send(71,&in)==RM_ERR_NOT_READY);
    task[2].state=TASK_WAITING; img->window_id=72;
    assert(resman_window_send(71,&in)==RM_ERR_NOT_READY);
    assert(resman_window_send(72,&in)==RM_OK);
    current_task=&task[2];
    assert(resman_window_poll(71,&out)==RM_ERR_NOT_READY);
    assert(resman_window_poll(72,&out)==1);
    current_task=&task[1]; assert(resman_text_host(0)==RM_OK);
    assert(resman_window_send(72,&in)==RM_ERR_NOT_READY);
    return 0;
}
