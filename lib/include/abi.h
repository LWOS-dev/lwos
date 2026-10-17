#ifndef _LW_ABI_H
#define _LW_ABI_H

/*
 * LWOS ABI 共享接口
 *
 * 提供方: ABI.BIN (abi/abi.c), 固定加载在 LW_ABI_BASE, 表在映像最开头
 * 调用方: 不链接提供方的任何符号, 只通过 lw_abi_base 查表调用
 *
 * slot 0x00-0x0F 同时是所有可加载映像的通用头 (MONITOR.BIN 也有),
 * loader 的 P 命令靠它校验、清 bss、换栈、跳转
 *
 * 唯一规则: slot 只追加。不改号、不复用、不删除。
 *           废弃的 slot 留空 (NULL), 编号永久占用。
 */

#include "stdint.h"
#include "dev/blockdev.h"

#define LW_ABI_MAGIC 0x4241574cu    /* "LWAB" */
#define LW_ABI_BASE  0x00100000u

/* ABI 复制此后端：read 非阻塞读取扫描码，wait 等待输入，LED 在前台更新。 */
typedef struct _LW_KBD_SOURCE {
    int (*read)(PBYTE scan);
    void (*wait)(void);
    void (*set_leds)(BYTE value);
} LW_KBD_SOURCE;

enum {
    // description of the image
    LW_SLOT_MAGIC               = 0x00,
    LW_SLOT_ENTRY               = 0x01,     /* 程序: 入口; ABI.BIN: 初始化函数 */
    LW_SLOT_BSS_START           = 0x02,
    LW_SLOT_BSS_END             = 0x03,
    LW_SLOT_STACK_TOP           = 0x04,

    // console output
    LW_SLOT_CONSOLE_PUTC        = 0x10,
    LW_SLOT_CONSOLE_PUTCA       = 0x11,
    LW_SLOT_CONSOLE_PUTS        = 0x12,
    LW_SLOT_CONSOLE_PUTS_PAD    = 0x13,
    LW_SLOT_CONSOLE_PUT_BYTE    = 0x14,
    LW_SLOT_CONSOLE_PUT_WORD    = 0x15,
    LW_SLOT_CONSOLE_PUT_DWORD   = 0x16,
    LW_SLOT_CONSOLE_PUT_QWORD   = 0x17,
    LW_SLOT_CONSOLE_CLEAR       = 0x18,
    LW_SLOT_CONSOLE_DUMP128     = 0x19,
    LW_SLOT_CONSOLE_PUT_DEC32   = 0x1a,
    LW_SLOT_CONSOLE_GETK        = 0x1b,
    LW_SLOT_CONSOLE_GETC        = 0x1c,
    LW_SLOT_CONSOLE_GETP        = 0x1d,
    LW_SLOT_CONSOLE_SET_CUR     = 0x1e,

    // devices
    // not out dx, al because this way may
    // create some JOURNEY
    LW_SLOT_IO_OUTB             = 0x20,
    LW_SLOT_IO_OUTW             = 0x21,
    LW_SLOT_IO_OUTL             = 0x22,
    LW_SLOT_IO_INB              = 0x23,
    LW_SLOT_IO_INW              = 0x24,
    LW_SLOT_IO_INL              = 0x25,
    LW_SLOT_IO_KBD_PROBE        = 0x26,
    LW_SLOT_IO_KBD_ENABLE       = 0x27,
    LW_SLOT_IO_VIDEO_ENTER      = 0x28,
    LW_SLOT_IO_VIDEO_EXIT       = 0x29,
    LW_SLOT_IO_VIDEO_LIVE       = 0x2a,
    LW_SLOT_IO_FPU_INIT         = 0x2b,
    LW_SLOT_IO_KBD_BIND_SOURCE  = 0x2c,

    LW_SLOT_INFO_FB             = 0x30,
    LW_SLOT_INFO_FB_W           = 0x31,
    LW_SLOT_INFO_FB_H           = 0x32,
    LW_SLOT_INFO_FB_BPP         = 0x33,
    LW_SLOT_INFO_FB_PITCH       = 0x34,

    LW_SLOT_PCI_PRESENT         = 0x40,
    LW_SLOT_PCI_READ_DWORD      = 0x41,
    LW_SLOT_PCI_WRITE_DWORD     = 0x42,

    LW_SLOT_DISK_PROBE          = 0x60,
    LW_SLOT_DISK_COUNT          = 0x61,
    LW_SLOT_DISK_INFO           = 0x62,
    LW_SLOT_DISK_READ           = 0x63,
    LW_SLOT_DISK_WRITE          = 0x64,
    LW_SLOT_DISK_STRERROR       = 0x65,

    LW_SLOT_BLK_COUNT           = 0x66,
    LW_SLOT_BLK_GET             = 0x67,

    LW_SLOT_RESV_ENTRY          = 0x7f,

    /* PM -> RM -> INT 10h -> PM; 仅用于低频 BIOS 视频调用。 */
    LW_SLOT_TRAMP_CALL          = 0x80,

    LW_SLOT_COUNT
};

/*
 * 调用方用法:
 *   PVOID *lw_abi_base;                  // 每个调用方程序定义一次
 *   if (lw_abi_attach((PVOID)LW_ABI_BASE) != 0) ...   // 校验 + 初始化
 *   lw_puts("hello\n\r");
 */
extern PVOID *lw_abi_base;

#define LW_ABI_VALID()      ((DWORD)lw_abi_base[LW_SLOT_MAGIC] == LW_ABI_MAGIC)
#define LW_CALL(slot, type) ((type)lw_abi_base[slot])

#define lw_putc         LW_CALL(LW_SLOT_CONSOLE_PUTC,       void (*)(char))
#define lw_putca        LW_CALL(LW_SLOT_CONSOLE_PUTCA,      void (*)(char))
#define lw_puts         LW_CALL(LW_SLOT_CONSOLE_PUTS,       void (*)(const char *))
#define lw_puts_pad     LW_CALL(LW_SLOT_CONSOLE_PUTS_PAD,   void (*)(const char *, int))
#define lw_put_byte     LW_CALL(LW_SLOT_CONSOLE_PUT_BYTE,   void (*)(BYTE))
#define lw_put_word     LW_CALL(LW_SLOT_CONSOLE_PUT_WORD,   void (*)(WORD))
#define lw_put_dword    LW_CALL(LW_SLOT_CONSOLE_PUT_DWORD,  void (*)(DWORD))
#define lw_put_qword    LW_CALL(LW_SLOT_CONSOLE_PUT_QWORD,  void (*)(QWORD))
#define lw_screen_clear LW_CALL(LW_SLOT_CONSOLE_CLEAR,      void (*)(void))
#define lw_dump128      LW_CALL(LW_SLOT_CONSOLE_DUMP128,    void (*)(PVOID))
#define lw_put_dec32    LW_CALL(LW_SLOT_CONSOLE_PUT_DEC32,  void (*)(DWORD))
#define lw_getk         LW_CALL(LW_SLOT_CONSOLE_GETK,       int (*)(void))
#define lw_getc         LW_CALL(LW_SLOT_CONSOLE_GETC,       char (*)(void))
#define lw_getp         LW_CALL(LW_SLOT_CONSOLE_GETP,       DWORD (*)(void))
#define lw_setcur       LW_CALL(LW_SLOT_CONSOLE_SET_CUR,    void (*)(int))

#define lw_outb         LW_CALL(LW_SLOT_IO_OUTB,            void (*)(WORD, BYTE))
#define lw_outw         LW_CALL(LW_SLOT_IO_OUTW,            void (*)(WORD, WORD))
#define lw_outl         LW_CALL(LW_SLOT_IO_OUTL,            void (*)(WORD, DWORD))
#define lw_inb          LW_CALL(LW_SLOT_IO_INB,             BYTE (*)(WORD))
#define lw_inw          LW_CALL(LW_SLOT_IO_INW,             WORD (*)(WORD))
#define lw_inl          LW_CALL(LW_SLOT_IO_INL,             DWORD (*)(WORD))
#define lw_kbd_probe    LW_CALL(LW_SLOT_IO_KBD_PROBE,       void (*)(void))
#define lw_kbd_enable   LW_CALL(LW_SLOT_IO_KBD_ENABLE,      int (*)(void))
#define lw_kbd_bind_source LW_CALL(LW_SLOT_IO_KBD_BIND_SOURCE, int (*)(const LW_KBD_SOURCE *))
#define lw_gfx_enter    LW_CALL(LW_SLOT_IO_VIDEO_ENTER,     int (*)(void))
#define lw_gfx_exit     LW_CALL(LW_SLOT_IO_VIDEO_EXIT,      void (*)(void))
#define lw_gfx_is_live  LW_CALL(LW_SLOT_IO_VIDEO_LIVE,      int (*)(void))
#define lw_tramp_call   LW_CALL(LW_SLOT_TRAMP_CALL,         WORD (*)(WORD, WORD, WORD, WORD))
#define lw_fpu_init     LW_CALL(LW_SLOT_IO_FPU_INIT,        void (*)(void))

#define lw_get_fb       LW_CALL(LW_SLOT_INFO_FB,            DWORD (*)(void))
#define lw_get_fb_w     LW_CALL(LW_SLOT_INFO_FB_W,          DWORD (*)(void))
#define lw_get_fb_h     LW_CALL(LW_SLOT_INFO_FB_H,          DWORD (*)(void))
#define lw_get_fb_bpp   LW_CALL(LW_SLOT_INFO_FB_BPP,        DWORD (*)(void))
#define lw_get_fb_pitch LW_CALL(LW_SLOT_INFO_FB_PITCH,      DWORD (*)(void))

#define lw_blk_count    LW_CALL(LW_SLOT_BLK_COUNT, BYTE (*)(void))
#define lw_blk_get      LW_CALL(LW_SLOT_BLK_GET, PBLKDEV (*)(BYTE))

#define lw_disk_probe   LW_CALL(LW_SLOT_DISK_PROBE,         void (*)(void))
#define lw_disk_count   LW_CALL(LW_SLOT_DISK_COUNT,         BYTE (*)(void))
#define lw_disk_info    LW_CALL(LW_SLOT_DISK_INFO,          PVOID (*)(BYTE))
#define lw_disk_read    LW_CALL(LW_SLOT_DISK_READ,          int (*)(BYTE, QWORD, DWORD, PVOID))
#define lw_disk_write   LW_CALL(LW_SLOT_DISK_WRITE,         int (*)(BYTE, QWORD, DWORD, PCVOID))
#define lw_disk_strerr  LW_CALL(LW_SLOT_DISK_STRERROR,      const char *(*)(int))

#define lw_pci_present  LW_CALL(LW_SLOT_PCI_PRESENT,        int (*)(void))
#define lw_pci_read     LW_CALL(LW_SLOT_PCI_READ_DWORD,     DWORD (*)(BYTE, BYTE, BYTE, BYTE))
#define lw_pci_write    LW_CALL(LW_SLOT_PCI_WRITE_DWORD,    void (*)(BYTE, BYTE, BYTE, BYTE, DWORD))

#define lw_resv_entry   LW_CALL(LW_SLOT_RESV_ENTRY,         void (*)(void))
// check LWAB
static inline int lw_abi_attach(PVOID base) {
    PVOID *t = (PVOID *)base;
    if ((DWORD)t[LW_SLOT_MAGIC] != LW_ABI_MAGIC)
        return -1;
    lw_abi_base = t;
    return ((int (*)(void))t[LW_SLOT_ENTRY])();
}

#endif
