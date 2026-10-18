
#include "abi.h"
#include "ctx.h"
#include "eth_tmp.h"
#include "fs_monitor.h"
#include "keys.h"
#include "tui.h"

// headers from common libraries
#include "mem.h"
#include "convert.h"
#include "string.h"
#include "dev/blockdev.h"
#include "bitmap.h"
#include "resman.h"

PVOID *lw_abi_base;
PVOID *rm_base=0;
FS_VOLUME monitor_fs;

/* ABI 不可用时连 puts 都没有, 只能直接写显存 */
static void no_abi_halt(void) {
    const char *msg = "NO ABI.BIN AT 100000H";
    PWORD video = (PWORD)0xB8000;
    for (int i = 0; msg[i]; i++) {
        video[i] = 0x0c00 | (BYTE)msg[i];
    }
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static char line[128];

static void prompt_read() {
    int n = 0;
    lw_puts(">");
    for (;;) {
        char c = lw_getc();
        if (c=='\n') { line[n]=0; lw_puts("\n\r"); return; }
        if (c=='\b') { if (n>0) { n--; lw_puts("\b \b"); } continue; }
        if (c<' '||c>'~'||n>=(int)sizeof(line)-1)
            continue;
        line[n++]=c;
        lw_putc(c);
    }
}

extern void pci_scan(void);
extern void pci_detail(BYTE bus,BYTE dev,BYTE fn);
extern int pci_find_class(BYTE cls, BYTE sub, PBYTE bus, PBYTE dev, PBYTE fn);

void help() {
    lw_puts("rm                      Start preloaded RESMAN and return\n\r");
    lw_puts("cb/s                    Clear buffer/screen\n\r");
    lw_puts("tb                      Type down buffer\n\r");
    lw_puts("tf                      Create file at buffered path (with parents)\n\r");
    lw_puts("sb                      Show buffer\n\r");
    lw_puts("br/bw    [devidx] [lba] Block device operation\n\r");
    lw_puts("         [addr] [count]\n\r");
    lw_puts("d        [addr]         Dump 128 bytes(PBYTE)\n\r");
    lw_puts("ob/ow/ol [addr] [value] Write to Memory\n\r");
    lw_puts("m                       List memory entry table\n\r");
    lw_puts("ib/iw/il [addr]         Read from Memory\n\r");
    lw_puts("n(r)                    Network debuging (single supported NIC)\n\r");
    lw_puts("p        <b> <d> <f>    PCI information\n\r");
    lw_puts("v(e/x)                  Video info, enter/exit gfx\n\r");
}

static void display_banner(void) {
    lw_puts(" ___       ___       __   ________  ________      \n\r");
    lw_puts("|\\  \\     |\\  \\     |\\  \\|\\   __  \\|\\   ____\\     \n\r");
    lw_puts("\\ \\  \\    \\ \\  \\    \\ \\  \\ \\  \\|\\  \\ \\  \\___|_    \n\r");
    lw_puts(" \\ \\  \\    \\ \\  \\  __\\ \\  \\ \\  \\\\\\  \\ \\_____  \\   \n\r");
    lw_puts("  \\ \\  \\____\\ \\  \\|\\__\\_\\  \\ \\  \\\\\\  \\|____|\\  \\  \n\r");
    lw_puts("   \\ \\_______\\ \\____________\\ \\_______\\____\\_\\  \\ \n\r");
    lw_puts("    \\|_______|\\|____________|\\|_______|\\_________\\\n\r");
    lw_puts("                                      \\|_________|\n\r");
    lw_puts("\n   LWOS MT v2 - New Technology Operating System\n\n\r");
}
static char var_path[256];
static void start_resman(void)
{
    PVOID *head = (PVOID *)LW_RM_BASE;
    DWORD entry = (DWORD)head[LW_RM_SLOT_ENTRY];
    DWORD bss_start = (DWORD)head[LW_RM_SLOT_BSS_START];
    DWORD bss_end = (DWORD)head[LW_RM_SLOT_BSS_END];
    static int prepared;

    if (head[LW_RM_SLOT_MAGIC] != (PVOID)LW_RM_MAGIC ||
        head[LW_RM_SLOT_IDENT] != (PVOID)LW_RM_IDENT ||
        entry < LW_RM_BASE + LW_RM_SLOT_COUNT * sizeof(PVOID) ||
        entry >= bss_start || bss_start > bss_end ||
        bss_end > LW_RM_LIMIT || head[LW_RM_SLOT_STACK_TOP] != 0) {
        lw_puts("RESMAN MISSING OR INVALID\n\r");
        return;
    }

    /* Keep state on subsequent calls; never reset a live resource manager. */
    if (!prepared) {
        memzero((PVOID)bss_start, bss_end - bss_start);
        prepared = 1;
    }
    int result = ((RESMAN_ENTRY)entry)(lw_abi_base);
    rm_base=(result==RM_OK)?head:0;
    lw_puts("RESMAN RETURNED: ");
    lw_put_dword((DWORD)result);
    lw_puts("\n\r");
}
static void execute(const char* str) {
    char c0;
    char c1;
    skip_ws(&str);
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD d;

    if (!*str)return;
    c0 = *str++;
	c1 = (*str&&*str!=' '&&*str!='\t')?
            *str++:0;

    switch (c0) {
        case 'm': {
            mmap_init();
            mmap_dump();
            break;
        }
        case 'c': {
            switch (c1) {
                case 's': {
                    lw_screen_clear();
                    break;
                }
                case 'b': {
                    memzero(var_path, 256);
                    break;
                }
            }
            break;
        }
        case 's': {
            switch (c1) {
                case 'b': {
                    lw_puts(var_path);
                    lw_puts("\n\r");
                    break;
                }
            }
            break;
        }
        case 't': {
            switch (c1) {
                case 'f': {
                    int err = fs_touch(&monitor_fs, var_path);
                    lw_puts(err ? "CREATE FILE FAILED: " : "FILE READY: ");
                    lw_puts(var_path);
                    if (err) {
                        lw_puts(" ERRLV=");
                        lw_put_dword(err);
                    }
                    lw_puts("\n\r");
                    break;
                }
                case 'b': {
                    memzero(var_path, 256);
                    int n=0;
                    for (;;) {
                        char c=lw_getc();
                        if (c=='\n') { var_path[n]=0; lw_puts("\n\r"); break; }
                        if (c=='\b') { if (n>0) { n--; lw_puts("\b \b"); } continue; }
                        if (c<' '||c>'~'||n>=(int)sizeof(var_path)-1)
                            continue;
                        var_path[n++]=c;
                        lw_putc(c);
                    }
                    break;
                }
            }
            break;
        }
        case 'n': {
            BYTE tb,td,tf;
            DWORD bar0;
            if (c1==0) {
                if (pci_find_class(2,0,&tb,&td,&tf)==0) {
                    lw_puts("NO ETHERNET DEVICE FOUND\n\r");
                    return;
                }
                set_eth_bdf(tb,td,tf);
                bar0=lw_pci_read(tb,td,tf,0x10);
                set_eth_bar(bar0);
                eth_init();
                return;
            }
            switch (c1) {
                case 'r': {
                    while (lw_getp()==0xffffffff) {
                        e1k_rx_poll();
                    }
                    break;
                }
                default: {
                    break;
                }
            }
            break;
        }
        case '.': { // new function test
            memcpy(var_path, "/boot.ini\0", 10);
            break;
        }
    }
}
const char config_file[]="/monitor.rc";
int mon_block=1;
__attribute__((section(".text.start")))
void monitor_main(void) {

    if (lw_abi_attach((PVOID)LW_ABI_BASE) != 0) {
        no_abi_halt();
    }
    lw_puts("\n\rLWOS MONITOR v2 COPYLEFT 2026\n\r");

    idt_init();
    mem_pool_init();
    if (fs_init(&monitor_fs) < 0)
        lw_puts("FILESYSTEM INIT FAILED\n\r");
    lw_kbd_probe();
    lw_kbd_enable();
    lw_fpu_init();

    start_resman();

    tui_init();
    tui_loop();
    asm volatile ("cli");
    while (1) {
        asm volatile ("hlt");
    }

    while (mon_block) {
        prompt_read();
        execute(line);
    }
}
