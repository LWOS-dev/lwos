#ifndef MKEXE_H
#define MKEXE_H

#include <stddef.h>
#include <stdint.h>
#include "../../lib/include/lwp.h"

#define MKEXE_DEFAULT_STACK (64u * 1024u)

typedef struct {
    const char *input_path;
    const char *output_path;
    uint32_t abi_need;
    uint32_t stack_size;
    uint32_t flags;
} MkexeOptions;

/* 宿主内存对象, 不是磁盘布局; buffer / image / relocs 由工具释放。 */
typedef struct {
    unsigned char *data;
    size_t size;
} MkexeBuffer;

typedef struct {
    LwpHdr header;
    unsigned char *image;
    uint32_t *relocs;
} MkexeImage;

/* 返回 0 成功, -1 错误; 错误说明输出到 stderr。
 * out 调用前清零, 所有退出路径均可使用对应 free 函数。
 */
int mkexe_read_file(const char *path, MkexeBuffer *out);
void mkexe_buffer_free(MkexeBuffer *buffer);
int mkexe_dump(const MkexeBuffer *file);

/* TODO: ELF32/i386/ET_EXEC, 链接基址 0, --emit-relocs。
 * PT_LOAD -> 映像; R_386_32 -> RVA 表; 内部 R_386_PC32 跳过。
 * 拒绝未定义符号、其他重定位及越界/重叠段。
 */
int mkexe_from_elf(const MkexeBuffer *elf, const MkexeOptions *options,
                   MkexeImage *out);
/* TODO: 按小端显式编码头及重定位表, 不直接 fwrite 宿主结构体。 */
int mkexe_write_file(const char *path, const MkexeImage *image);
void mkexe_image_free(MkexeImage *image);

#endif
