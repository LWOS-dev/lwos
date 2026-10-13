
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
    lw_puts("RESMAN RETURNED: ");
    lw_put_dword((DWORD)result);
    lw_puts("\n\r");
}
FS_FILE vi_file;
PBYTE vi_buf;
void edit_hex() {
    vi_buf=(PVOID)0x400000;
    // monitor环境还没开启资源管理，可能没有写动态分配的必要
    if (vi_buf==0)vi_buf=(PVOID)0x400000;
    int loop=1;
    int t;
    int col=0,line=0;
    int ptr=0;
    int bits=0; // 0none 1low4 2high4
    DWORD current_length=vi_file.length;
    BYTE db_tmp;
    DWORD offset=0;
    DWORD readcnt,writecnt;
    DWORD pos;
    int select=0;
    // 0 : offset
    // 1 : datas
    // 2 : ctrls
    char tstr_dw[9];
    char tstr_db[3];
    lw_setcur(0);
    lw_screen_clear();
    for (int i=0; i<80; i++) {
        *(video+i)=0x7000;
    }
    vi_attr=0x70;
    putxy("PRESS ANYKEY TO CONTINUE", 26, 12);
    putxy("LWREAL HEX EDITOR V1.0\0", 3, 0);
    vi_attr=0x07;
    while (loop) {

        t=lw_getk();
        vi_attr=0x07;
        putxy("LENGTH=", 20, 1);
        hex_xlat(current_length, tstr_dw, 8);
        putxy(tstr_dw, 27, 1);
        switch (t) {
            case VK_TAB: {
                select++;
                select%=3;
                db_tmp=0;
                bits=0;
                break;
            }
            case VK_ESC: {
                //return;
                break;
            }
        }
        if (select==0 || select==1) {
            switch (t) {
                case VK_PGUP: {
                    offset-=0x100;
                    if ((int)offset<=0)offset=0;
                    break;
                }
                case VK_PGDN: {
                    offset+=0x100;
                    break;
                }
            }
        }
        if (select==1) {
            switch (t) {
                case VK_UP: {
                    if (line<=0)break;
                    line--;
                    break;
                }
                case VK_DOWN: {
                    if (line>=15)break;
                    line++;
                    break;
                }
                case VK_LEFT: {
                    if (col<=0)break;
                    col--;
                    break;
                }
                case VK_RIGHT: {
                    if (col>=15)break;
                    col++;
                    break;
                }
            }
            pos=offset+col+line*16;
            switch (bits) {
                case 0: {
                    if (hex_digit(t)!=0xff) {
                        db_tmp=hex_digit(t);
                        bits=1;
                    }
                    break;
                }
                case 1: {
                    if (hex_digit(t)!=0xff) {
                        db_tmp=db_tmp<<4;
                        db_tmp|=hex_digit(t)&0xf;
                        bits=2;
                    }
                    break;
                }
                case 2: {
                    if (t==VK_ESC) {
                        bits=0;
                        db_tmp=0;
                    }
                    if (t==VK_ENTER) {
                        bits=0;
                        /* 跳过 EOF 编辑时，将中间空隙填零。 */
                        if (pos>current_length)
                            memzero(vi_buf+current_length,pos-current_length);
                        vi_buf[pos]=db_tmp;
                        db_tmp=0;
                        if (pos>=current_length)current_length=pos+1;
                    }
                    break;
                }
            }
        }
        if (select==2) {
            switch (t) {
                case VK_F1: {
                    readcnt=0;
                    int err=fs_file_seek(&vi_file,0,FS_SEEK_SET);
                    if (!err)
                        err=fs_file_read(&vi_file,vi_buf,vi_file.length,&readcnt);
                    if (!err && readcnt==vi_file.length)
                        current_length=vi_file.length;
                    putxy("                                                ",1,21);
                    putxy(err || readcnt!=vi_file.length ? "LOAD FAILED" : "LOADED",1,21);
                    break;
                }
                case VK_F2: {
                    /* length 由文件系统维护；编辑后的长度通过 count 传入。 */
                    writecnt=0;
                    int err=fs_file_seek(&vi_file,0,FS_SEEK_SET);
                    if (!err)
                        err=fs_file_write(&vi_file,vi_buf,current_length,&writecnt);
                    if (!err && writecnt==current_length)
                        err=fs_sync(&monitor_fs);
                    putxy("                                                                ",1,21);
                    putxy(err || writecnt!=current_length ? "SAVE FAILED" : "SAVED",1,21);
                    putxy("ERR=",15,21);
                    hex_xlat((DWORD)err,tstr_dw,8);
                    putxy(tstr_dw,19,21);
                    putxy("WRITTEN=",29,21);
                    hex_xlat(writecnt,tstr_dw,8);
                    putxy(tstr_dw,37,21);
                    break;
                }
                case VK_F3: {
                    loop=0;
                    break;
                }
            }
        }
        vi_attr=(select==0)?0x70:0x07;
        hex_xlat(offset, tstr_dw, 8);
        putxy("OFFSET=", 1, 1);
        putxy(tstr_dw, 8, 1);
        
        for (int i=0; i<16; i++) {
            vi_attr=(i==col)?0x0f:0x07;
            putxy("+", 10+i*3, 2);
            hex_xlat(i, tstr_db, 1);
            tstr_db[1]=0;
            putxy(tstr_db, 11+i*3, 2);
        }

        vi_attr=(select==2)?0x07:0x08;
        putxy("F1=LOAD  F2=SAVE  F3=EXIT", 1, 24);
        for (int i=0; i<16; i++) {
            for (int j=0; j<16; j++) {
                ptr=offset+i*16+j;
                hex_xlat(vi_buf[i*16+j+offset], tstr_db, 2);
                vi_attr=((col==j)&&(line==i))?((select==1)?0x70:0x07):0x08;
                if ((DWORD)ptr>=current_length) {
                    putxy("   ", 10+j*3, 3+i);
                } else {
                    putxy(tstr_db, 10+j*3, 3+i);
                    putxy(" ", 12+j*3, 3+i);
                }
                if ((DWORD)ptr==(offset+col+line*16)) {
                    if (bits==1) {
                        hex_xlat(db_tmp, tstr_db, 2);
                        tstr_db[0]=' ';
                        putxy(tstr_db, 10+j*3, 3+i);
                        putxy("*", 12+j*3, 3+i);
                    }
                    if (bits==2) {
                        hex_xlat(db_tmp, tstr_db, 2);
                        putxy(tstr_db, 10+j*3, 3+i);
                        putxy("*", 12+j*3, 3+i);
                    }
                }
            }
            vi_attr=(i==line)?0x0f:0x07;
            hex_xlat(offset+i*16, tstr_dw, 8);
            putxy(tstr_dw, 1, 3+i);
        }
    }
    fs_file_close(&vi_file);
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
        case 'r': {
            if (c1 == 'm') start_resman();
            else lw_puts("USAGE: rm\n\r");
            break;
        }
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
                    if (vi_file.opened) {
                        lw_puts("CLOSE EDITOR FILE FIRST\n\r");
                        break;
                    }
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
                case 't': {
                    if (!hex_parse(&str, &a)) {
                        lw_puts("TRACE FAT CHAIN\n\r");
                        return;
                    }
                    DWORD cc=a,cn=0;
                    lw_put_dword(cc);
                    lw_puts("\n\r");
                    while (next_cluster(&monitor_fs,cc,&cn)==1) {
                        lw_puts("  ->");
                        lw_put_dword(cn);
                        lw_puts("\n\r");
                        cc=cn;
                        cn=0;
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
        case 'e': {
            if (!c1) {
                lw_puts("eh EDIT HEX FILE\n\r");
                lw_puts("ep EDIT PLAIN TEXT\n\r");
                return;
            }
            switch (c1) {
                case 'h': {
                    int err=fs_file_open(&monitor_fs, var_path, &vi_file);
                    lw_puts("EDIT HEX FILE : ");
                    lw_puts(var_path);
                    lw_puts(" ERRLV=");
                    lw_put_dword(err);
                    lw_puts("\n\r");
                    if (err) {
                        lw_puts("FAIL\n\r");
                        return;
                    }
                    lw_puts("FILE SIZE=");
                    lw_put_dword(vi_file.length);
                    lw_puts("\n\r");
                    lw_getc();
                    edit_hex();
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
