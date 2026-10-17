/*
 * ABI.BIN: 服务表 + 初始化入口
 * 这是一个库映像, 没有主循环也没有自己的栈, 用调用者的栈
 * 函数声明来自 dev/ 的头文件; 这里只 extern 链接脚本符号
 */

#include "abi.h"
#include "stdint.h"
#include "text.h"
#include "io.h"
#include "kbd.h"
#include "binfo.h"
#include "tramp.h"
#include "fpu.h"
#include "pci.h"
#include "dev/ata.h"

extern char __bss_start[], __bss_end[];

int abi_init(void);

/* 启动阶段探测并注册; 后续 AHCI 可在这里追加注册步骤。 */
static void disk_probe(void) {
    blkdev_reset();
    ata_detect_quiet();
    if (ata_register_blockdevs() < 0)
        puts("BLOCK DEVICE TABLE FULL\n\r");
}

const PVOID lw_abi[LW_SLOT_COUNT] __attribute__((section(".abi"), used, aligned(16))) = {
    [LW_SLOT_MAGIC]     = (PVOID)LW_ABI_MAGIC,
    [LW_SLOT_ENTRY]     = (PVOID)abi_init,
    [LW_SLOT_BSS_START] = (PVOID)__bss_start,
    [LW_SLOT_BSS_END]   = (PVOID)__bss_end,
    [LW_SLOT_STACK_TOP] = 0,    /* 库映像, 无自己的栈 */

    [LW_SLOT_CONSOLE_PUTC]      = (PVOID)putc,
    [LW_SLOT_CONSOLE_PUTCA]     = (PVOID)putca,
    [LW_SLOT_CONSOLE_PUTS]      = (PVOID)puts,
    [LW_SLOT_CONSOLE_PUTS_PAD]  = (PVOID)puts_pad,
    [LW_SLOT_CONSOLE_PUT_BYTE]  = (PVOID)put_byte,
    [LW_SLOT_CONSOLE_PUT_WORD]  = (PVOID)put_word,
    [LW_SLOT_CONSOLE_PUT_DWORD] = (PVOID)put_dword,
    [LW_SLOT_CONSOLE_PUT_QWORD] = (PVOID)put_qword,
    [LW_SLOT_CONSOLE_PUT_DEC32] = (PVOID)put_dec,
    [LW_SLOT_CONSOLE_CLEAR]     = (PVOID)screen_clear,
    [LW_SLOT_CONSOLE_DUMP128]   = (PVOID)dump128,
    [LW_SLOT_CONSOLE_GETK]      = (PVOID)getk,
    [LW_SLOT_CONSOLE_GETC]      = (PVOID)getc,
    [LW_SLOT_CONSOLE_GETP]      = (PVOID)kbd_poll,
    [LW_SLOT_CONSOLE_SET_CUR]   = (PVOID)setcur,

    [LW_SLOT_IO_OUTB]           = (PVOID)outb,
    [LW_SLOT_IO_OUTW]           = (PVOID)outw,
    [LW_SLOT_IO_OUTL]           = (PVOID)outl,
    [LW_SLOT_IO_INB]           = (PVOID)inb,
    [LW_SLOT_IO_INW]           = (PVOID)inw,
    [LW_SLOT_IO_INL]           = (PVOID)inl,
    [LW_SLOT_IO_KBD_PROBE]      = (PVOID)kbd_probe,
    [LW_SLOT_IO_KBD_ENABLE]     = (PVOID)kbd_enable,
    [LW_SLOT_IO_KBD_BIND_SOURCE] = (PVOID)kbd_bind_source,
    [LW_SLOT_IO_VIDEO_ENTER]    = (PVOID)gfx_enter,
    [LW_SLOT_IO_VIDEO_EXIT]     = (PVOID)gfx_exit,
    [LW_SLOT_IO_VIDEO_LIVE]     = (PVOID)gfx_live,
    [LW_SLOT_IO_FPU_INIT]       = (PVOID)fpu_init,

    [LW_SLOT_INFO_FB]           = (PVOID)bi_fb,
    [LW_SLOT_INFO_FB_W]         = (PVOID)bi_fb_w,
    [LW_SLOT_INFO_FB_H]         = (PVOID)bi_fb_h,
    [LW_SLOT_INFO_FB_BPP]       = (PVOID)bi_fb_bpp,
    [LW_SLOT_INFO_FB_PITCH]     = (PVOID)bi_fb_pitch,

    [LW_SLOT_PCI_PRESENT]       = (PVOID)pci_present,
    [LW_SLOT_PCI_READ_DWORD]    = (PVOID)pci_read_dword,
    [LW_SLOT_PCI_WRITE_DWORD]   = (PVOID)pci_write_dword,

    [LW_SLOT_DISK_PROBE]        = (PVOID)disk_probe,
    [LW_SLOT_DISK_COUNT]        = (PVOID)ata_count,
    [LW_SLOT_DISK_INFO]         = (PVOID)ata_get,
    [LW_SLOT_DISK_READ]         = (PVOID)ata_read,
    [LW_SLOT_DISK_WRITE]        = (PVOID)ata_write,
    [LW_SLOT_DISK_STRERROR]     = (PVOID)ata_strerror,

    [LW_SLOT_BLK_COUNT]         = (PVOID)blkdev_count,
    [LW_SLOT_BLK_GET]           = (PVOID)blkdev_get,

    [LW_SLOT_RESV_ENTRY]        = (PVOID)binfo_dump,
    [LW_SLOT_TRAMP_CALL]        = (PVOID)tramp_call
};

/* 自己清自己的 bss, 调用方不需要知道 ABI.BIN 的内存布局 */
int abi_init(void) {
    static int initialized;
    /* loader 首次调用前已清 BSS；后续 APP attach 不重置共享资源。 */
    if (initialized)return 0;
    for (char *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
    initialized=1;
    return 0;
}
