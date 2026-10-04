
extern char __bss_start[], __bss_end[];

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef unsigned char* uint8_p;
typedef unsigned short* uint16_p;
typedef unsigned int* uint32_p;
typedef unsigned long* uint64_p;
typedef void* uint0_p;

#include "abi.h"
#include "bootdisk.h"

#define NULL (0)

uint0_p *lw_abi_base = NULL;

unsigned int curx = 0, cury = 0;

uint16_t *video = (uint16_t*)0xB8000;

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}



static const char _hex[]="0123456789ABCDEF";

void update_cursor() {
    unsigned int pos = curx + cury * 80;
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xff);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xff);
}

// 硬件光标是唯一可信的位置, ABI.BIN 等外部代码也会移动它
void get_cursor() {
    unsigned int pos;
    outb(0x3D4, 0x0E);
    pos = inb(0x3D5) << 8;
    outb(0x3D4, 0x0F);
    pos |= inb(0x3D5);

    if (pos >= 80 * 25) pos = 0;

    curx = pos % 80;
    cury = pos / 80;
}

void screen_scroll() {
    for (int i = 0; i < 80 * 24; i++) {
        video[i] = video[i + 80];
    }
    for (int i = 0; i < 80; i++) {
        video[80*24+i] = 0x0b00;
    }
}

void screen_clear() {
    for (int i = 0; i < 80 * 25; i++) {
        video[i] = 0x0b00;
    }
    curx = 0;
    cury = 0;
    update_cursor();
}

void putc(char c) {
    get_cursor();
    switch (c) {
        case 0x8: {
            if (curx >= 1)curx--;
            break;
        }
        case 0xa: {
            cury += 1;
            break;
        }
        case 0xd: {
            curx = 0;
            break;
        }
        default: {
            video[curx+cury*80] = (uint16_t)0x0b00 | (uint8_t)c;
            curx++;
            break;
        }
    }
    if (curx >= 80) {
        curx = 0;
        cury++;
    }
    if (cury >= 25) {
        cury = 24;
        screen_scroll();
    }
    update_cursor();
}

void puts(char* s) {
    while (*s) {
        putc(*s);
        s++;
    }
}

void put_byte(uint8_t v) {
    putc(_hex[(v >> 4) & 0xf]);
    putc(_hex[v & 0xf]);
}
void put_word(uint16_t v) {
    putc(_hex[(v >> 12) & 0xf]);
    putc(_hex[(v >> 8) & 0xf]);
    putc(_hex[(v >> 4) & 0xf]);
    putc(_hex[v & 0xf]);
}
void put_dword(uint32_t v) {
    putc(_hex[(v >> 28) & 0xf]);
    putc(_hex[(v >> 24) & 0xf]);
    putc(_hex[(v >> 20) & 0xf]);
    putc(_hex[(v >> 16) & 0xf]);
    putc(_hex[(v >> 12) & 0xf]);
    putc(_hex[(v >> 8) & 0xf]);
    putc(_hex[(v >> 4) & 0xf]);
    putc(_hex[v & 0xf]);
}
uint8_t par_num(char c) {
    uint8_t i = 0;
    switch (c) {
        case '0':i=0x0;break;
        case '1':i=0x1;break;
        case '2':i=0x2;break;
        case '3':i=0x3;break;
        case '4':i=0x4;break;
        case '5':i=0x5;break;
        case '6':i=0x6;break;
        case '7':i=0x7;break;
        case '8':i=0x8;break;
        case '9':i=0x9;break;
        case 'A':case 'a':i=0xa;break;
        case 'B':case 'b':i=0xb;break;
        case 'C':case 'c':i=0xc;break;
        case 'D':case 'd':i=0xd;break;
        case 'E':case 'e':i=0xe;break;
        case 'F':case 'f':i=0xf;break;
        default:i=0;break;
    }
    return i;
}
uint32_t par_dword(const char *s) {
    uint32_t ret = 0;
    for (int i = 0; i < 8; i++) {
        ret = ret << 4;
        ret |= par_num(s[i]);
    }
    return ret;
}

void putca(char c) {
    if ((c >= 0x20) && ((unsigned char)c <= 0x7f)) {
        putc(c);
    } else {
        putc('.');
    }
}

void dump128(void* src) {
    uint32_t addr = (uint32_t)src;
    for (int i = 0; i < 8; i++) {
        put_dword(addr);
        puts("|");
        for (int j = 0; j < 8; j++) {
            put_byte(*(uint8_p)(addr+j));
            putc(' ');
        }
        puts("\b-");
        for (int j = 0; j < 8; j++) {
            put_byte(*(uint8_p)(addr+j+8));
            putc(' ');
        }
        puts("\b|");
        for (int j = 0; j < 16; j++) {
            putca(*(uint8_p)(addr+j));
        }
        puts("\n\r");
        addr += 16;
    }
}

void *memcpy(void *dst, void *src, int count)
{
    void * ret = dst;
    while (count--) {
        *(char *)dst = *(char *)src;
        dst = (char *)dst + 1;
        src = (char *)src + 1;
    }
    return ret;
}
void *memzero(void *dst, int len) {
    uint8_p p = (uint8_p)dst;
    while (len-- > 0) {
        *(p++)=0;
    }
    return dst;
}
int strcmp(const char *s1, const char *s2) {
	while (*s1 == *s2++)
		if (*s1++ == 0)
			return (0);
	return (*(const uint8_t *)s1 - *(const uint8_t *)--s2);
}
unsigned int strlen(const char *s) {
    const char *p = s;
    while (*p)
        p++;
    return (unsigned int)(p - s);
}
char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++))
        ;
    return ret;
}

/* 启动期磁盘读取由 stage2 的 BIOS 跳板完成, 不再访问 IDE 端口。 */
static void disk_read_checked(QWORD lba, DWORD count, PVOID destination) {
    int status = boot_disk_read(lba, count, destination);
    if (status == 0)
        return;
    puts("BIOS DISK READ FAILED, LBA ");
    put_dword((uint32_t)lba);
    puts(" STATUS ");
    put_dword((uint32_t)status);
    puts("\n\r");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static uint8_t fat_buf[512];

typedef struct {
    uint8_t flag; // 80h -> active, 00h -> inactive
    uint8_t chs_beg[3]; // old method
    uint8_t series; // partition series
    uint8_t chs_end[3];
    uint32_t lba;
    uint32_t lba_count;
} MBR_PTE; // MBR partition table entry

uint8_t num_fat = 0;
uint16_t fat_size = 0; // in sectors
uint32_t start_lba = 0;
uint32_t root_cluster = 0;
uint16_t reserved_sector = 0;
uint32_t cluster_to_lba(uint32_t c) {
    return (c - 2) + start_lba;
}

MBR_PTE mbrpte[4];
int part_entry = 0;

int fat_init() {
    memzero(fat_buf, 512);
    // BIOS 使用 stage2 保存的启动盘号
    disk_read_checked(0, 1, (uint16_p)fat_buf);

    memcpy(mbrpte, fat_buf+0x1be, 16*4);

    for (int i = 0; i < 4; i++) {
        if (mbrpte[i].flag == 0x80) {
            putc('P');
            putc(_hex[i]);
            puts(" ACTIVE; ");

            if (mbrpte[i].series == 0x0c) {
                puts("FAT32, ");
            } else {
                puts("UNK FS, ");
            }

            puts("BEG LBA AT ");
            put_dword(mbrpte->lba);
            putc('.');
            put_dword(mbrpte->lba_count);
            puts("\r\n");

            part_entry = i;
            goto done;
        }
    }

    done:
    disk_read_checked(mbrpte[part_entry].lba, 1, (uint16_p)fat_buf);

    if (*(uint16_p)(fat_buf+0x1fe) != 0xaa55) {
        puts("BAD PARITION\r\n");
        return -2;
    }

    if (*(fat_buf+0xd) != 1) {
        puts("INCORRECT MAKE FAT TOOLS\r\n");
        return -3;
    }

    num_fat = *(uint8_p)(fat_buf+0x10);
    fat_size = *(uint16_p)(fat_buf+0x24);
    reserved_sector = *(uint16_p)(fat_buf+0x0e);
    puts("NUM_FATS ");
    put_dword(num_fat);
    puts("\n\rFATS_SIZE ");
    put_dword(fat_size);
    puts("\n\r");
    // get root cluster
    start_lba = num_fat*fat_size+mbrpte[part_entry].lba+reserved_sector;

    root_cluster = *(uint32_p)(fat_buf+0x2c);
    puts("ROOT CLUSTER: ");
    put_dword(root_cluster);
    puts("\r\n");

    disk_read_checked(cluster_to_lba(root_cluster), 1, (uint16_p)fat_buf);

    return 0;
}

typedef struct {
    char name[11]; // 8.3 = 11bytes
    uint8_t attr;
    uint32_t cluster;
    uint32_t length;
} SFTE; // simplified file table entries

int fat_read_fte(int index, SFTE* p) {
    char display_name[12];
    disk_read_checked(cluster_to_lba(root_cluster+index/16), 1, (uint16_p)fat_buf);

    memcpy(p->name, fat_buf+(index%16)*32+0, 11);
    p->attr = *(uint8_p)(fat_buf+(index % 16)*32+0x11);
    p->cluster = *(uint16_p)(fat_buf+(index%16)*32+0x14) << 16 |
                 *(uint16_p)(fat_buf+(index%16)*32+0x1a);
    p->length = *(uint32_p)(fat_buf+(index%16)*32+0x1c);

    if (p->name[0] != 0) {
        puts("FILE: ");
        memcpy(display_name, p->name, 11);
        display_name[11]=0;
        puts(display_name);
        puts("  CLUSTER AT ");
        put_dword(p->cluster);
        puts("  LENGTH ");
        put_dword(p->length);
        puts("\n\r");
        return 0;
    }
    puts("END\n\r");
    return -1;
}

void search_file(const char* name, uint32_p cluster, uint32_p size) {
    int idx=0;
    SFTE e;
    memzero(&e, sizeof(e));
    char fixed_name[12];
    char resc_fixed[12];
    memcpy(fixed_name, (uint8_p)name, 11);
    fixed_name[11]=0;
    
    while (fat_read_fte(idx++, &e) == 0) {
        memcpy(resc_fixed, (uint8_p)e.name, 11);
        resc_fixed[11]=0;
        if (strcmp(fixed_name, resc_fixed) == 0) {
            *cluster = e.cluster;
            *size = e.length;
            return;
        }
    }

    *cluster = 0;
}

uint32_t next_cluster(uint32_t cluster) {
    disk_read_checked(mbrpte[part_entry].lba+reserved_sector+cluster/128, 1, (uint16_p)fat_buf);

    uint32_t res = ((uint32_p)fat_buf)[cluster%128] & 0x0fffffff;

    put_dword(cluster);
    puts("->");
    put_dword(res);
    puts("\n\r");

    return res;
}

void load_cluster_to_memory(void* dst, uint32_t cluster) {
    uint16_p p = dst;
    uint32_t cur_cluster = cluster;
    do {
        disk_read_checked(cluster_to_lba(cur_cluster), 1, p);
        p+=256;
        cur_cluster = next_cluster(cur_cluster);
    } while (cur_cluster <= 0x0fffffef);
}

uint32_t bootini_c = 0;

/*
line_reading -> tokenize -> parse -> dispatch
buffer      line        token    imi         handle
*/

uint32_t lr_ptr = 0;
uint32_t lr_size = 0;

int line_reading(const char *str, char* out) {
    // catching for '['  end with ']'
    while (*(str+lr_ptr) != '[') {
        if (*(str+lr_ptr) == 0)return 0;
        lr_ptr++;
    }
    lr_ptr++;
    int count=0;
    while (*(str+lr_ptr) != ']') {
        *(out+count) = *(str+lr_ptr);
        if (*(str+lr_ptr) == 0)return 0;
        count++;
        lr_ptr++;
    }
    *(out+count)=0;
    return count;
}

static const char *const head_names[] = {
    "MAGIC    ",
    "ENTRY    ",
    "BSS START",
    "BSS END  ",
    "STACK TOP"
};

static int head_check(uint32_p h) {
    puts("IMAGE HEAD AT ");
    put_dword((uint32_t)h);
    puts("\n\r");

    for (int i = LW_SLOT_MAGIC; i <= LW_SLOT_STACK_TOP; i++) {
        puts(" ");
        puts((char*)head_names[i]);
        puts(" ");
        put_dword(h[i]);
        puts("\n\r");
    }

    if (h[LW_SLOT_MAGIC] != LW_ABI_MAGIC) {
        puts("BAD MAGIC\n\r");
        return -1;
    }

    // entry shouldnot be 0 or inside bss
    if (h[LW_SLOT_ENTRY] <= (uint32_t)h ||
        h[LW_SLOT_ENTRY] >= h[LW_SLOT_BSS_START]) {
        puts("BAD ENTRY\n\r");
        return -1;
    }
    if (h[LW_SLOT_BSS_END] < h[LW_SLOT_BSS_START]) {
        puts("BAD BSS\n\r");
        return -1;
    }
    return 0;
}

uint32_t execute(const char* str) {
    char buf[20];
    char filename[12];
    char addr_str[9];
    memzero(buf, 20);
    memzero(filename, 12);
    memzero(addr_str, 9);

    int len = strlen(str);
    memcpy(buf, (uint0_p)str, len);

    memcpy(filename, buf+9, 11);
    memcpy(addr_str, buf+1, 8);

    uint32_t address = par_dword(addr_str);

    uint32_t cluster = 0;
    uint32_t size = 0;

    switch (str[0]) {
        case 'A':case 'a': {
            // attach ABI
            uint32_p h = (uint32_p)address;
            if (head_check(h) != 0)break;
            if (h[LW_SLOT_STACK_TOP] != 0) {
                puts("NOT LIB, USE [P]\n\r");
                break;
            }

            // zero the bss
            memzero((uint8_p)h[LW_SLOT_BSS_START],
                    h[LW_SLOT_BSS_END] - h[LW_SLOT_BSS_START]);
            
            int r = ((int (*)(void))h[LW_SLOT_ENTRY])();

            if (r != 0) {
                puts("INIT FAILED ");
                put_dword(r);
                puts("\n\r");
                break;
            }

            lw_abi_base = (void**)h;
            puts("ABI ATTACHED\n\r");

            break;
        }
        case 'D':case 'd': {
            dump128((uint0_p)address);
            break;
        }
        case 'L':case 'l': {
            search_file(filename, &cluster, &size);
            if (cluster == 0) {
                puts("NO SUCH FILES\n\r");
                break;
            }
            load_cluster_to_memory((uint0_p)address, cluster);
            puts("LOADED TO ");
            put_dword(address);
            puts(" WITH ");
            put_dword(size);
            puts(" BYTES\n\r");
            break;
        }
        case 'C':case 'c': {
            disk_read_checked(mbrpte[part_entry].lba+reserved_sector+address/128, 1, (uint16_p)fat_buf);

            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 8; j++) {
                    put_dword(((uint32_p)fat_buf)[i*8+j]);
                    putc(' ');
                }
                puts("\n\r");
            }
            break;
        }
        case 'P':case 'p': {
            // port image
            // check, zero bss
            // change stack
            // set entry
            // never return
            uint32_p h = (uint32_p)address;
            if (head_check(h) != 0)break;

            if (h[LW_SLOT_STACK_TOP] == 0) {
                puts("NOT PROGRAM, USE [A]\n\r");
                break;
            }

            if (lw_abi_base == NULL) {
                puts("NO ABI ATTACHED\n\r");
            }

            memzero((uint8_p)h[LW_SLOT_BSS_START], h[LW_SLOT_BSS_END]-h[LW_SLOT_BSS_START]);

            puts("JUMP\n\r");
            asm volatile (
                "mov  %0, %%esp\n\t"
                "xor  %%ebp, %%ebp\n\t"
                "push %2\n\t"
                "call *%1\n\t"
                "1: cli\n\t"
                "hlt\n\t"
                "jmp 1b\n\t"
                :
                : "r"(h[LW_SLOT_STACK_TOP]), "r"(h[LW_SLOT_ENTRY]), "r"(lw_abi_base)
                : "memory"
            );

            __builtin_unreachable();
        }
        case 'Q':case 'q': {
            return 1;
            break;
        }
        case 'R':case 'r': {
            // call address unsafely
            // return if 
            // really a ret there

            puts("TRY RUN AT ");
            put_dword(address);
            puts("\n\r");

            uint0_p target = (uint0_p)address;
            asm volatile (
                "call *%0"
                :
                : "r" (target)
                : "memory"
            );

            puts("RETURNED\n\r");
            break;
        }
        default: {
            break;
        }
    }

    return 0;
}

char line_buf[20];

__attribute__((section(".text.start")))
void loader_main(void) {

    for (char *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }

    screen_clear();
    puts("FILESYSTEM INITIALIZE ...\n\r");
    if (fat_init() != 0) {
        puts("FILESYSTEM INIT FAILED\n\r");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    puts("LOADING BOOT.INI ...\n\r");
    search_file("BOOT    INI", &bootini_c, &lr_size);
    if (!bootini_c || lr_size > 0x400u) {
        puts("BOOT.INI MISSING OR TOO LARGE\n\r");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    load_cluster_to_memory((uint0_p)0x500, bootini_c);

    do {
        line_reading((const char*)0x500, line_buf);
    } while (execute(line_buf) == 0);

    while (1);

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}