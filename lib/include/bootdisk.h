#ifndef _LW_BOOTDISK_H
#define _LW_BOOTDISK_H

#include "stdint.h"

/* stage2 与 loader 的启动期约定; 不依赖 ABI.BIN。
 * DAP、中转缓冲区、跳板参数和实模式栈均位于保留的低内存。
 */
#define BOOT_DISK_DAP_BASE    0x90f0u
#define BOOT_DISK_AX_ADDR     0xa018u
#define BOOT_DISK_FLAGS_ADDR  0xa026u
#define BOOT_DISK_VEC_ADDR    0xa030u
#define BOOT_DISK_BUFFER      0xd000u
#define BOOT_DISK_SECTOR_SIZE 512u

typedef struct __attribute__((packed)) _BOOT_DISK_DAP {
    BYTE size, reserved;
    WORD count;
    WORD offset, segment;
    QWORD lba;
} BOOT_DISK_DAP;

/* 0 成功; 正值是 BIOS AH 错误码; -1 参数/向量错误。
 * 每次读一扇区到低内存, 再复制到调用方目的地。仅启动阶段可用。
 */
int boot_disk_read(QWORD lba, DWORD count, PVOID destination);

#endif
