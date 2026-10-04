#include "bootdisk.h"

_Static_assert(sizeof(BOOT_DISK_DAP) == 16, "BIOS DAP size");
_Static_assert(__builtin_offsetof(BOOT_DISK_DAP, lba) == 8, "BIOS DAP LBA");

int boot_disk_read(QWORD lba, DWORD count, PVOID destination) {
    DWORD vector = *(volatile DWORD *)BOOT_DISK_VEC_ADDR;
    QWORD bytes = (QWORD)count * BOOT_DISK_SECTOR_SIZE;
    QWORD begin = (DWORD)destination;
    QWORD end = begin + bytes;
    /* 低于 64KB 是启动代码和 BIOS 工作区, 不作为读盘目的地。
     * 唯一例外是 loader 原有的 BOOT.INI 缓冲区 0x500..0x900。
     */
    if (!vector || !destination || !count || end > 0x100000000ull ||
        lba > ~(QWORD)0 - (count - 1) ||
        (begin < 0x10000u && !(begin >= 0x500u && end <= 0x900u)))
        return -1;

    volatile BOOT_DISK_DAP *dap = (volatile BOOT_DISK_DAP *)BOOT_DISK_DAP_BASE;
    PBYTE out = destination;
    for (DWORD i = 0; i < count; i++) {
        int status = 0;
        /* BIOS 可能修改 count, 重试前必须重新构造整个 DAP。 */
        for (int attempt = 0; attempt < 3; attempt++) {
            dap->size = 0x10;
            dap->reserved = 0;
            dap->count = 1;
            dap->offset = BOOT_DISK_BUFFER;
            dap->segment = 0;
            dap->lba = lba + i;
            ((void (*)(void))vector)();
            WORD flags = *(volatile WORD *)BOOT_DISK_FLAGS_ADDR;
            WORD ax = *(volatile WORD *)BOOT_DISK_AX_ADDR;
            if (!(flags & 1) && dap->count == 1) {
                status = 0;
                break;
            }
            status = (ax >> 8) ? (ax >> 8) : 1;
        }
        if (status)
            return status;
        PCBYTE source = (PCBYTE)BOOT_DISK_BUFFER;
        for (DWORD j = 0; j < BOOT_DISK_SECTOR_SIZE; j++)
            out[j] = source[j];
        out += BOOT_DISK_SECTOR_SIZE;
    }
    return 0;
}
