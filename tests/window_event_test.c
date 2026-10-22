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
    /* Defaults are set before task publication; later only the owner may edit. */
    img->state=RM_IMAGE_LOADED;
    assert(resman_image_set_caption(img,"/DIR/APP.EXE")==RM_OK);
    assert(img->caption[0]=='/' && img->caption[12]==0);
    img->state=RM_IMAGE_BOUND;
    assert(resman_image_set_caption(img,"wrong")==RM_ERR_ARGUMENT);
    assert(resman_window_set_caption(71,"wrong")==RM_ERR_NOT_READY);
    current_task=&task[2];
    char caption[]="Custom caption";
    assert(resman_window_set_caption(71,caption)==RM_OK);
    caption[0]='X';
    assert(img->caption[0]=='C');
    assert(resman_window_set_caption(70,"wrong")==RM_ERR_NOT_READY);
    assert(resman_window_set_caption(71,0)==RM_ERR_ARGUMENT);
    char long_caption[LW_WINDOW_CAPTION_SIZE+1];
    for (unsigned i=0;i<LW_WINDOW_CAPTION_SIZE;i++)long_caption[i]='L';
    long_caption[LW_WINDOW_CAPTION_SIZE]=0;
    assert(resman_window_set_caption(71,long_caption)==RM_ERR_ARGUMENT);
    assert(img->caption[0]=='C');
    long_caption[LW_WINDOW_CAPTION_SIZE-1]=0;
    assert(resman_window_set_caption(71,long_caption)==RM_OK);
    assert(img->caption[LW_WINDOW_CAPTION_SIZE-2]=='L');
    assert(img->caption[LW_WINDOW_CAPTION_SIZE-1]==0);
    assert(resman_window_set_caption(71,"")==RM_OK && img->caption[0]==0);
    assert(resman_window_set_caption(71,"Retained")==RM_OK);
    current_task=&task[1];
    RM_TEXT_SNAPSHOT snap={0};
    snap.caption[0]='!';
    assert(resman_text_snapshot(0,&snap)==RM_OK && snap.caption[0]=='!');
    assert(resman_text_snapshot_caption(0,&snap)==RM_OK && snap.caption[0]=='R');
    task[2].state=TASK_DEAD;
    current_task=&task[2];
    assert(resman_window_set_caption(71,"dead")==RM_ERR_NOT_READY);
    current_task=&task[1];
    assert(resman_text_snapshot_caption(0,&snap)==RM_OK && snap.caption[0]=='R');
    task[2].state=TASK_WAITING;
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
