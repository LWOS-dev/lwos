#ifndef _LW_LWP_H
#define _LW_LWP_H

#include "stdint.h"

/* LWP v1 磁盘格式: 小端, 所有 off 均为文件偏移, RVA 相对映像基址。
 * 文件头不属于映像; 扇区读取的尾部填充不计入 image_size。
 * 不含导入表; ABI / resman / DLL 调用通过函数表完成。
 */
#define LWP_MAGIC       0x0050574cu
#define LWP_VERSION     1u
#define LWP_HEADER_SIZE 0x40u
#define LWP_FLAG_DLL    0x01u
#define LWP_FLAG_FPU    0x02u
/* v1 工具暂不支持固定地址映像, 没有存放加载基址的字段。 */
#define LWP_FLAG_FIXED  0x04u

typedef struct _LWP_HDR {
    DWORD magic;
    WORD version;
    WORD hdr_size;
    DWORD flags;
    DWORD abi_need;       /* 运行时 ABI 至少要有的 slot 数 */
    DWORD entry;          /* 入口 RVA */
    DWORD image_off;
    DWORD image_size;     /* text + rodata + data, 包含段间填充 */
    DWORD mem_size;       /* image_size + bss, 不含独立栈 */
    DWORD stack_size;     /* DLL 为 0, 使用调用方栈 */
    DWORD reloc_off;
    DWORD reloc_count;
    DWORD sym_off;        /* 格式待定, 当前必须为 0 */
    DWORD sym_count;
    DWORD export_off;     /* 格式待定, 表 DLL 暂不需要 */
    DWORD export_count;
    DWORD crc32;          /* 映像字节的 CRC32; 0 表示不校验 */
} LwpHdr, *PLwpHdr;

/* 每项是需要加加载基址的 32 位字的 RVA, 允许未对齐。
 * 必须满足 rva + 4 <= image_size, 同一位置不能重复修正。
 */
typedef DWORD LwpReloc;

_Static_assert(sizeof(LwpHdr) == LWP_HEADER_SIZE, "LWP header size");
_Static_assert(sizeof(LwpReloc) == 4, "LWP relocation size");

#endif
