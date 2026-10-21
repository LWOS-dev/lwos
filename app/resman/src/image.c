#include "../include/resman.h"
#include "abi.h"
#include "mem.h"

RM_IMAGE images[RM_MAX_IMAGES];

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
        env->size < sizeof(LW_ENV))return RM_ERR_ARGUMENT;
    
    stack_base=(DWORD)image->stack;

    if (image->stack_size > 0xffffffffu-stack_base)return RM_ERR_ARGUMENT;

    stack_top=(stack_base+image->stack_size)& ~15u;

    if (stack_top <= stack_base ||
        stack_top - stack_base < 32) {
        return RM_ERR_ARGUMENT;
    }

    image->state=RM_IMAGE_RUNNING;

    result=image_call(image->entry,stack_top,env);

    image->state=RM_IMAGE_LOADED;
    *exit_code=result;
    return RM_OK;
}
int resman_image_unload(PRM_IMAGE image) {
    if (!image_handle_valid(image))return RM_ERR_ARGUMENT;
    if (image->state!=RM_IMAGE_LOADED)return RM_ERR_ARGUMENT;
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
