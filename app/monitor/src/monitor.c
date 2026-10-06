
#include "abi.h"
#include "ctx.h"
#include "eth_tmp.h"
#include "fs_monitor.h"

// headers from common libraries
#include "mem.h"
#include "convert.h"
#include "string.h"
#include "dev/blockdev.h"
#include "bitmap.h"

PVOID *lw_abi_base;
static FS_VOLUME monitor_fs;

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

static int fb_w, fb_h, fb_pitch;
static PDWORD framebuffer;
static char line[128];

void putpixel(int x, int y, DWORD color) {
    if((x>=fb_w)||(x<0)||(y>=fb_h)||(y<0))return;
    framebuffer[x+y*fb_pitch]=color&0xffffff;
}

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
    lw_puts("cb/s                    Clear buffer/screen\n\r");
    lw_puts("tb                      Type down buffer\n\r");
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
        case 'a': {
            switch (c1) {
                case 'p': {
                    lw_puts("DISK PROBE\n\r");
                    lw_disk_probe();
                    return;
                }
                case 'r': {
                    if (!hex_parse(&str,&a)||
                    !hex_parse(&str,&b)||
                    !hex_parse(&str,&c)||
                    !hex_parse(&str,&d)) {
                        lw_puts("SYNTAX ERROR\n\r");
                        return;
                    }
                    lw_disk_read(a,b,d,(PVOID)c);
                    lw_puts("READ  ");
                    lw_put_dword(d);
                    lw_puts(" SECTOR(S) FROM LBA ");
                    lw_put_dword(b);
                    lw_puts(" TO [");
                    lw_put_dword(c);
                    lw_puts("] AT DEV ");
                    lw_put_byte(a);
                    lw_puts("\n\r");
                    break;
                }
                case 'w': {
                    if (!hex_parse(&str,&a)||
                    !hex_parse(&str,&b)||
                    !hex_parse(&str,&c)||
                    !hex_parse(&str,&d)) {
                        lw_puts("SYNTAX ERROR\n\r");
                        return;
                    }
                    lw_disk_write(a,b,d,(PVOID)c);
                    lw_puts("WRITE ");
                    lw_put_dword(d);
                    lw_puts(" SECTOR(S) TO LBA ");
                    lw_put_dword(b);
                    lw_puts(" FROM [");
                    lw_put_dword(c);
                    lw_puts("] AT DEV ");
                    lw_put_byte(a);
                    lw_puts("\n\r");
                    break;
                }
                case 0: {
                    lw_puts("USAGE: ar/w [DEV] [LBA] [TARGET] [COUNT]\n\r");
                    break;
                }
            }
            break;
        }
        case 'b': {
            lw_puts("BLOCK DEVICE\n\r");

            break;
        }
        case 'h': {
            help();
            break;
        }
        case 'f': {
            switch (c1) {
                case 'i': {
                    if (fs_init(&monitor_fs) < 0)
                        lw_puts("FILESYSTEM INIT FAILED\n\r");
                    break;
                }
                case 'l': {
                    if (!hex_parse(&str, &a)) {
                        lw_puts("DIR CLUSTER IS REQUIRED\n\r");
                        return;
                    }
                    dir_list(&monitor_fs, a);
                    break;
                }
            }
            break;
        }
        case 'd': {
            if (!hex_parse(&str, &a)) {
                return;
            }
            lw_dump128((PVOID)a);
            break;
        }
        case 'o': { // out/write
            if (!hex_parse(&str, &a)) {
                return;
            }
            if (!hex_parse(&str,&b)) {
                return;
            }
            switch (c1) {
                case 'b': {
                    a&=0xffffffff;
                    lw_put_dword(a);
                    lw_puts(":");
                    lw_put_byte(b);
                    lw_puts("\n\r");
                    *(PBYTE)(a)=(BYTE)b;
                    if (*(PBYTE)a!=(BYTE)b) {
                        lw_puts("WROTE FAIL\n\r");
                        lw_put_dword(a);
                        lw_puts(":");
                        lw_put_byte(*(PBYTE)a);
                        lw_puts("\n\r");
                    }
                    break;
                }
                case 'w': {
                    a&=0xfffffffe;
                    lw_put_dword(a);
                    lw_puts(":");
                    lw_put_word(b);
                    lw_puts("\n\r");
                    *(PWORD)(a)=(WORD)b;
                    if (*(PWORD)a!=(WORD)b) {
                        lw_puts("WROTE FAIL\n\r");
                        lw_put_dword(a);
                        lw_puts(":");
                        lw_put_word(*(PWORD)a);
                        lw_puts("\n\r");
                    }
                    break;
                }
                case 'l': {
                    a&=0xfffffffc;
                    lw_put_dword(a);
                    lw_puts(":");
                    lw_put_dword(b);
                    lw_puts("\n\r");
                    *(PDWORD)(a)=(DWORD)b;
                    if (*(PDWORD)a!=(DWORD)b) {
                        lw_puts("WROTE FAIL\n\r");
                        lw_put_dword(a);
                        lw_puts(":");
                        lw_put_byte(*(PDWORD)a);
                        lw_puts("\n\r");
                    }
                    break;
                }
                default:return;
            }
            break;
        }
        case 'i': { // in/read
            DWORD d;
            if (!hex_parse(&str, &a)) {
                return;
            }
            switch (c1) {
                case 'b': {
                    a&=0xffffffff;
                    d=*(PBYTE)a;
                    lw_put_dword(a);
                    lw_puts(":");
                    lw_put_byte(d);
                    lw_puts("\n\r");
                    break;
                }
                case 'w': {
                    a&=0xfffffffe;
                    d=*(PWORD)a;
                    lw_put_dword(a);
                    lw_puts(":");
                    lw_put_word(d);
                    lw_puts("\n\r");
                    break;
                }
                case 'l': {
                    a&=0xfffffffc;
                    d=*(PDWORD)a;
                    lw_put_dword(a);
                    lw_puts(":");
                    lw_put_dword(d);
                    lw_puts("\n\r");
                    break;
                }
                default:return;
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
        case 'p': {
            DWORD bus, dev, fn;
            if (!hex_parse(&str,&bus)) {
                pci_scan();
                return;
            }
            if (!hex_parse(&str,&dev) || !hex_parse(&str, &fn)) {
                pci_scan();
                return;
            }
            if (bus > 0xFF || dev > 31 || fn > 7) {
                lw_puts("INVALID BDF\n\r");
                return;
            }
            pci_detail((BYTE)bus,(BYTE)dev,(BYTE)fn);
            break;
        }
        case 'v': {
            int rc;
            if (c1==0) {
                lw_puts("GFX ");
                lw_puts(lw_gfx_is_live()?"LIVE":"OFF ");
                lw_puts("\n\r");
                lw_puts("FRAMEBUF=");
                lw_put_dword((DWORD)lw_get_fb());
                lw_puts(" ");
                lw_put_word((DWORD)lw_get_fb_w());
                lw_puts("x");
                lw_put_word((DWORD)lw_get_fb_h());
                lw_puts(" ");
                lw_put_byte((DWORD)lw_get_fb_bpp());
                lw_puts("BIT PITCH=");
                lw_put_dword((DWORD)lw_get_fb_pitch()/4);
                lw_puts("*DWORD\n\r");
            } else if (c1=='e') {
                rc=lw_gfx_enter();
                if (rc) {
                    lw_puts(rc==-1?"NO USABLE MODE IN BOOT INFO\n\r":
                            "ROM REFUSED THE MODE\n\r");
                    return;
                }
            } else if (c1=='x') {
                lw_gfx_exit();
            }
            break;
        }
        case '.': { // new function test
            kmalloc(123);
            break;
        }
    }
}

__attribute__((section(".text.start")))
void monitor_main(void) {

    if (lw_abi_attach((PVOID)LW_ABI_BASE) != 0) {
        no_abi_halt();
    }
    lw_puts("\n\rLWOS MONITOR v2 COPYLEFT 2026\n\r");

    idt_init();
    mem_pool_init();
    lw_kbd_probe();
    lw_kbd_enable();
    lw_fpu_init();

    fb_h = lw_get_fb_h();
    fb_w = lw_get_fb_w();
    fb_pitch = lw_get_fb_pitch()/4;
    framebuffer = (PDWORD)lw_get_fb();

    lw_resv_entry();

    while (1) {
        prompt_read();
        execute(line);
    }
}
