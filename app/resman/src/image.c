#include "../include/resman.h"
#include "abi.h"
#include "mem.h"
#include "../include/task.h"
#include "../include/keyboard.h"

RM_IMAGE images[RM_MAX_IMAGES];
static DWORD text_host_id, next_window_id;
/* Synchronous calls use their caller's task, including kill/reap lifetime. */
static DWORD call_task[RM_MAX_IMAGES];

void resman_text_task_gone(DWORD id) {
    if (text_host_id==id)text_host_id=0;
}
int resman_text_available(void) {
    return text_host_id && task[text_host_id].state != TASK_FREE &&
        task[text_host_id].state != TASK_DEAD;
}
int resman_text_host(int enable) {
    DWORD flags=mem_irq_save();
    int rc=RM_ERR_NOT_READY;
    if (current_task && current_task->id) {
        if (enable && (!resman_text_available() || text_host_id==current_task->id)) {
            /* F7 hosts must acquire input just like the boot autoload host.
             * Keep host publication and ownership in one IRQ-masked section. */
            rc=resman_keyboard_set_owner(current_task->id);
            if (rc==RM_OK)text_host_id=current_task->id;
        } else if (!enable && text_host_id==current_task->id) {
            text_host_id=0;
            rc=RM_OK;
        }
    }
    mem_irq_restore(flags);
    return rc;
}
static int text_snapshot(DWORD slot, RM_TEXT_SNAPSHOT *out, int caption) {
    if (!out || slot>=RM_MAX_IMAGES)return RM_ERR_ARGUMENT;
    DWORD flags=mem_irq_save();
    PRM_IMAGE img=&images[slot];
    int rc=RM_ERR_NOT_READY;
    if (resman_text_available() && current_task->id==text_host_id &&
        img->state==RM_IMAGE_BOUND && img->text.buffer) {
        out->window_id=img->window_id;
        out->task_id=img->text_task;
        out->task_state=task[img->text_task].state;
        out->exit_code=task[img->text_task].exit_code;
        out->cursor=img->text.cursor;
        memcpy(out->cells,img->text.buffer,sizeof(out->cells));
        if (caption)memcpy(out->caption,img->caption,sizeof(out->caption));
        rc=RM_OK;
    }
    mem_irq_restore(flags);
    return rc;
}

/* Keep the old slot's 4016-byte write size for already-built GUI hosts. */
int resman_text_snapshot(DWORD slot, RM_TEXT_SNAPSHOT *out) {
    return text_snapshot(slot,out,0);
}
int resman_text_snapshot_caption(DWORD slot, RM_TEXT_SNAPSHOT *out) {
    return text_snapshot(slot,out,1);
}

int resman_image_acquire(PRM_IMAGE *out) {
    if (!out)return RM_ERR_ARGUMENT;
    *out=0;
    for (DWORD i = 0; i < RM_MAX_IMAGES; i++) {
        PRM_IMAGE image = &images[i];

        if (image->state != RM_IMAGE_FREE)
            continue;

        memzero(image, sizeof(*image));
        image->state = RM_IMAGE_LOADING;
        *out=image;
        return RM_OK;
    }

    return RM_ERR_NOT_READY; /* 槽位用完 */
}

int resman_image_load(PCVOID file, DWORD file_size, WORD owner, PRM_IMAGE *out) {
    if (!out)return -1;
    *out=0;
    if (!file)return -1;
    if (file_size<sizeof(LwpHdr)) {
        return -1; // size mismatch
    }
    PRM_IMAGE pimg;
    const LwpHdr *plhdr=file;
    PVOID base=0,stack=0;

    if (plhdr->magic != LWP_MAGIC)return -2;
    if (plhdr->version != LWP_VERSION)return -3;
    if (plhdr->hdr_size != LWP_HEADER_SIZE)return -4;
    DWORD visual=plhdr->flags & (LWP_FLAG_NWIN|LWP_FLAG_TEXT|LWP_FLAG_GFX);
    if (visual && (visual & (visual-1)))return RM_ERR_FORMAT;
    if ((plhdr->flags & LWP_FLAG_DLL) && visual)return RM_ERR_FORMAT;
    if (visual==LWP_FLAG_GFX)return RM_ERR_NOT_IMPLEMENTED;
    if (visual==LWP_FLAG_TEXT && !resman_text_available())return RM_ERR_NOT_READY;
    if (plhdr->abi_need > LW_SLOT_COUNT)return RM_ERR_ABI;
    if (!plhdr->mem_size || plhdr->mem_size>0x7ffff000u ||
        plhdr->stack_size<32 || plhdr->stack_size>0x7ffff000u)
        return RM_ERR_ARGUMENT;
    if (plhdr->image_size > plhdr->mem_size)return -7;
    if (plhdr->entry >= plhdr->image_size)return -8;

    if (plhdr->image_off > file_size)return -9;
    if (plhdr->image_size > (file_size-plhdr->image_off))return -9;
    if (plhdr->reloc_off > file_size)return -10;
    if (plhdr->reloc_count > (file_size-plhdr->reloc_off)/sizeof(LwpReloc))return -10;

    /* 先验证整张表, 避免分配后才发现错误。表项及修正位置均可未对齐。 */
    PCBYTE relocs=(PCBYTE)file+plhdr->reloc_off;
    for (DWORD i=0; i<plhdr->reloc_count; i++) {
        DWORD rva;
        memcpy(&rva, relocs+i*sizeof(LwpReloc), sizeof(rva));
        if (plhdr->image_size<sizeof(DWORD) ||
            rva>plhdr->image_size-sizeof(DWORD))return -10;
        /* 不要求输入表有序, 但同一个字不能被修正两次。 */
        for (DWORD j=0; j<i; j++) {
            DWORD previous;
            memcpy(&previous, relocs+j*sizeof(LwpReloc), sizeof(previous));
            if ((rva>=previous ? rva-previous : previous-rva)<sizeof(DWORD))
                return -10;
        }
    }

    int err=resman_image_acquire(&pimg);
    if (err)return -11;

    base=kmalloc(plhdr->mem_size);
    if (!base || base==(PVOID)-1 || base==(PVOID)-2) {
        memzero(pimg,sizeof(*pimg));
        return RM_ERR_MEMORY;
    }
    stack=kmalloc(plhdr->stack_size);
    if (!stack || stack==(PVOID)-1 || stack==(PVOID)-2) {
        kfree(base);
        memzero(pimg,sizeof(*pimg));
        return RM_ERR_MEMORY;
    }
    if (visual==LWP_FLAG_TEXT) {
        pimg->text.buffer=kmalloc(LW_TEXT_COLS*LW_TEXT_LINES*sizeof(WORD));
        if (!pimg->text.buffer || pimg->text.buffer==(PVOID)-1 || pimg->text.buffer==(PVOID)-2) {
            kfree(stack);
            kfree(base);
            memzero(pimg,sizeof(*pimg));
            return RM_ERR_MEMORY;
        }
        pimg->text.cols=LW_TEXT_COLS;
        pimg->text.lines=LW_TEXT_LINES;
        for (DWORD i=0;i<LW_TEXT_COLS*LW_TEXT_LINES;i++)
            ((PWORD)pimg->text.buffer)[i]=0x0720;
        if (!++next_window_id)++next_window_id;
        pimg->window_id=next_window_id;
    }
    memcpy(base,(PCBYTE)file+plhdr->image_off,plhdr->image_size);
    memzero((PBYTE)base+plhdr->image_size,plhdr->mem_size-plhdr->image_size);

    /* 文件内已保存链接到 0 的地址; 每个指定的 32 位字只加一次加载基址。 */
    for (DWORD i=0; i<plhdr->reloc_count; i++) {
        DWORD rva, value;
        memcpy(&rva, relocs+i*sizeof(LwpReloc), sizeof(rva));
        memcpy(&value, (PBYTE)base+rva, sizeof(value));
        value+=(DWORD)base;
        memcpy((PBYTE)base+rva, &value, sizeof(value));
    }

    pimg->base=base;
    pimg->image_size=plhdr->image_size;
    pimg->mem_size=plhdr->mem_size;
    pimg->entry=(DWORD)base+plhdr->entry;
    pimg->flags=plhdr->flags;
    pimg->stack=stack;
    pimg->stack_size=plhdr->stack_size;
    pimg->owner=owner;
    pimg->state=RM_IMAGE_LOADED;

    *out=pimg;
    return RM_OK;
}
static int image_handle_valid(PRM_IMAGE img) {
    for (DWORD i=0; i<RM_MAX_IMAGES; i++) {
        if (img==&images[i])return 1;
    }
    return 0;
}
extern int image_call(DWORD entry, DWORD stack_top, PCLW_ENV env);
static PRM_IMAGE window_find(DWORD window);
int resman_image_run(PRM_IMAGE image, PCLW_ENV env, int* exit_code) {
    DWORD stack_base, stack_top;
    int result;
    LW_ENV default_env;

    if (!exit_code)return RM_ERR_ARGUMENT;

    if (env==0) {
        memzero(&default_env,sizeof(LW_ENV));
        default_env.version=LW_ENV_VERSION;
        default_env.size=sizeof(LW_ENV);
        default_env.abi=lw_abi_base;
        default_env.rm=(PVOID*)lw_rm;
        default_env.argc=0;
        default_env.argv=0;
        env=&default_env;
    }

    *exit_code=0;

    if (!image_handle_valid(image))return RM_ERR_ARGUMENT;

    if (image->state!=RM_IMAGE_LOADED)return RM_ERR_ARGUMENT;
    if (!image->base || !image->stack || !image->entry)return RM_ERR_ARGUMENT;
    if (env->version != LW_ENV_VERSION || 
        env->size < LW_ENV_BASE_SIZE)return RM_ERR_ARGUMENT;
    if (image->flags & LWP_FLAG_TEXT) {
        /* Synchronous children borrow the caller's terminal and task identity.
         * Their private image buffer never becomes a published host window. */
        PRM_IMAGE terminal=window_find(env->vi.window_id);
        if (!current_task || !terminal || terminal->text_task!=current_task->id ||
            env->vi.size<sizeof(LW_VI) || env->vi.mode!=LW_VI_TEXT ||
            env->vi.txt!=&terminal->text)return RM_ERR_ARGUMENT;
    }
    
    stack_base=(DWORD)image->stack;

    if (image->stack_size > 0xffffffffu-stack_base)return RM_ERR_ARGUMENT;

    stack_top=(stack_base+image->stack_size)& ~15u;

    if (stack_top <= stack_base ||
        stack_top - stack_base < 32) {
        return RM_ERR_ARGUMENT;
    }

    image->state=RM_IMAGE_RUNNING;
    call_task[image-images]=current_task ? current_task->id : 0;

    result=image_call(image->entry,stack_top,env);

    call_task[image-images]=0;
    image->state=RM_IMAGE_LOADED;
    *exit_code=result;
    return RM_OK;
}
void resman_image_reap_calls(DWORD task_id) {
    if (!task_id)return;
    /* The caller has already verified that this task is DEAD. Nested calls
     * cannot return, so their stacks and images can now be released. */
    for (DWORD i=0;i<RM_MAX_IMAGES;i++) {
        if (call_task[i]==task_id && images[i].state==RM_IMAGE_RUNNING) {
            call_task[i]=0;
            images[i].state=RM_IMAGE_LOADED;
            resman_image_unload(&images[i]);
        }
    }
}
int resman_image_unload(PRM_IMAGE image) {
    if (!image_handle_valid(image))return RM_ERR_ARGUMENT;
    if (image->state!=RM_IMAGE_LOADED)return RM_ERR_ARGUMENT;
    if (image->text.buffer)kfree(image->text.buffer);
    if (image->stack) {
        kfree(image->stack);
        image->stack=0;
    }
    if (image->base) {
        kfree(image->base);
        image->base=0;
    }
    memzero(image,sizeof(*image));
    return RM_OK;
}
int resman_image_valid(PRM_IMAGE pimg) {
    return image_handle_valid(pimg);
}
/*
struct _RM_IMAGE {
    PVOID base;
    DWORD image_size, mem_size;
    DWORD entry, flags;
    PVOID stack;
    DWORD stack_size;
    WORD owner, state;
};
*/

/* The host chooses focus; resman only transports events to the window owner.
 * All lookups and queue operations run with IRQs masked, including wait's
 * empty-check/block sequence, so delivery cannot race a task going to sleep.
 */
static PRM_IMAGE window_find(DWORD window) {
    if (!window)return 0;
    for (DWORD i=0;i<RM_MAX_IMAGES;i++) {
        PRM_IMAGE img=&images[i];
        if (img->state==RM_IMAGE_BOUND && img->text.buffer &&
            img->window_id==window && img->text_task<TASK_MAX) {
            DWORD state=task[img->text_task].state;
            if (state==TASK_READY || state==TASK_RUNNING || state==TASK_WAITING)
                return img;
        }
    }
    return 0;
}
int resman_window_send(DWORD window, const LW_WINDOW_EVENT *event) {
    if (!event || (event->type!=LW_WINDOW_KEY && event->type!=LW_WINDOW_FOCUS) ||
        (event->type==LW_WINDOW_FOCUS && event->value!=0 && event->value!=1) ||
        (event->type==LW_WINDOW_KEY && (event->value<0 || event->value>=VK_LBUTTON)))
        return RM_ERR_ARGUMENT;
    DWORD flags=mem_irq_save();
    int rc=RM_ERR_NOT_READY;
    PRM_IMAGE img=window_find(window);
    if (current_task && resman_text_available() &&
        current_task->id==text_host_id && img) {
        DWORD next=(img->event_head+1)%LW_WINDOW_QUEUE_SIZE;
        if (next!=img->event_tail) {
            img->events[img->event_head]=*event;
            img->event_head=next;
            task_wake_locked(img->text_task,TASK_WAIT_WINDOW);
            rc=RM_OK;
        }
    }
    mem_irq_restore(flags);
    return rc;
}
int resman_window_poll(DWORD window, LW_WINDOW_EVENT *event) {
    if (!event)return RM_ERR_ARGUMENT;
    DWORD flags=mem_irq_save();
    PRM_IMAGE img=window_find(window);
    int rc=RM_ERR_NOT_READY;
    if (current_task && img && current_task->id==img->text_task) {
        rc=0;
        if (img->event_head!=img->event_tail) {
            *event=img->events[img->event_tail];
            img->event_tail=(img->event_tail+1)%LW_WINDOW_QUEUE_SIZE;
            rc=1;
        }
    }
    mem_irq_restore(flags);
    return rc;
}
int resman_window_wait(DWORD window) {
    DWORD flags=mem_irq_save();
    int rc=RM_ERR_NOT_READY;
    PRM_IMAGE img=window_find(window);
    if ((flags&0x200) && current_task && img && current_task->id==img->text_task) {
        while (img->event_head==img->event_tail)
            task_block_locked(TASK_WAIT_WINDOW);
        rc=RM_OK;
    }
    mem_irq_restore(flags);
    return rc;
}


static int copy_caption(PCHAR destination, PCSTR caption) {
    if (!caption)return RM_ERR_ARGUMENT;
    DWORD length=0;
    while (length<LW_WINDOW_CAPTION_SIZE && caption[length])length++;
    if (length==LW_WINDOW_CAPTION_SIZE)return RM_ERR_ARGUMENT;
    /* Validate first, then copy, so rejection leaves the old value intact. */
    memcpy(destination,caption,length+1);
    for (DWORD i=length+1;i<LW_WINDOW_CAPTION_SIZE;i++)destination[i]=0;
    return RM_OK;
}
int resman_image_set_caption(PRM_IMAGE image, PCSTR caption) {
    DWORD flags=mem_irq_save();
    int rc=RM_ERR_ARGUMENT;
    if (resman_image_valid(image) && image->state==RM_IMAGE_LOADED)
        rc=copy_caption(image->caption,caption);
    mem_irq_restore(flags);
    return rc;
}
int resman_window_set_caption(DWORD window, PCSTR caption) {
    DWORD flags=mem_irq_save();
    PRM_IMAGE image=window_find(window);
    int rc=RM_ERR_NOT_READY;
    if (current_task && image && current_task->id==image->text_task)
        rc=copy_caption(image->caption,caption);
    mem_irq_restore(flags);
    return rc;
}
