#include "stdint.h"
#include "text.h"
#include "io.h"
#include "kbd.h"

WORD curx = 0, cury = 0;
PWORD video = (PWORD)0xB8000;
static const char _hex[]="0123456789ABCDEF";

void set_cursor() {
    DWORD pos = curx + cury * 80;
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xff);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xff);
}

void get_cursor() {
    DWORD pos = 0;
    outb(0x3D4, 0x0E); // high
    pos = inb(0x3D5) << 8;
    outb(0x3D4, 0x0F); // low
    pos |= inb(0x3D5);

    if (pos >= 80 * 25) pos = 0;    // 光标不在屏幕内, 从头开始

    curx = pos % 80;
    cury = pos / 80;
}

void screen_scroll() {
    for (int i = 0; i < 80 * 24; i++) {
        video[i] = video[i + 80];
    }
    for (int i = 0; i < 80; i++) {
        video[80*24+i] = 0x0a00;
    }
}

void screen_clear() {
    for (int i = 0; i < 80 * 25; i++) {
        video[i] = 0x0a00;
    }
    curx = 0;
    cury = 0;
    set_cursor();
}

// 只改 curx/cury 和显存, 不碰 CRTC; 调用方负责同步
static void putc_raw(char c) {
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
            video[curx+cury*80] = (WORD)0x0a00 | (BYTE)c;
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
}
void setcur(int is_open) {
    outb(0x3D4, 0x0A);
    unsigned char value = inb(0x3D5);

    outb(0x3D4, 0x0A);
    outb(0x3D5, is_open ? (value & ~0x20) : (value | 0x20));
}
void putc(char c) {
    get_cursor();
    putc_raw(c);
    set_cursor();
}
void put_dec(DWORD value) {
    DWORD divisor = 1000000000;
    int started = 0;

    while (divisor)
    {
        DWORD digit = value / divisor;

        if (digit || started || divisor == 1)
        {
            putc('0' + digit);
            started = 1;
        }

        value %= divisor;
        divisor /= 10;
    }
}
void putca(char c) {
    if ((c >= 0x20) && ((unsigned char)c <= 0x7f)) {
        putc(c);
    } else {
        putc('.');
    }
}

void puts(const char* s) {
    get_cursor();       // 整串只同步一次
    while (*s) {
        putc_raw(*s);
        s++;
    }
    set_cursor();
}

void puts_pad(const char* s, int width) {
    get_cursor();
    int n=0;
    while (*s!=0) {
        putc_raw(*(s++));
        n++;
    }
    while (n++<width) {
        putc_raw(' ');
    }
    set_cursor();
}


void put_byte(BYTE v) {
    putc(_hex[(v >> 4) & 0xf]);
    putc(_hex[v & 0xf]);
}
void put_word(WORD v) {
    putc(_hex[(v >> 12) & 0xf]);
    putc(_hex[(v >> 8) & 0xf]);
    putc(_hex[(v >> 4) & 0xf]);
    putc(_hex[v & 0xf]);
}
void put_dword(DWORD v) {
    putc(_hex[(v >> 28) & 0xf]);
    putc(_hex[(v >> 24) & 0xf]);
    putc(_hex[(v >> 20) & 0xf]);
    putc(_hex[(v >> 16) & 0xf]);
    putc(_hex[(v >> 12) & 0xf]);
    putc(_hex[(v >> 8) & 0xf]);
    putc(_hex[(v >> 4) & 0xf]);
    putc(_hex[v & 0xf]);
}
void put_qword(QWORD v) {
    putc(_hex[(v >> 60) & 0xf]);
    putc(_hex[(v >> 56) & 0xf]);
    putc(_hex[(v >> 52) & 0xf]);
    putc(_hex[(v >> 48) & 0xf]);
    putc(_hex[(v >> 44) & 0xf]);
    putc(_hex[(v >> 40) & 0xf]);
    putc(_hex[(v >> 36) & 0xf]);
    putc(_hex[(v >> 32) & 0xf]);
    putc(_hex[(v >> 28) & 0xf]);
    putc(_hex[(v >> 24) & 0xf]);
    putc(_hex[(v >> 20) & 0xf]);
    putc(_hex[(v >> 16) & 0xf]);
    putc(_hex[(v >> 12) & 0xf]);
    putc(_hex[(v >> 8) & 0xf]);
    putc(_hex[(v >> 4) & 0xf]);
    putc(_hex[v & 0xf]);
}

void dump128(PVOID src) {
    DWORD addr = (DWORD)src;
    for (int i = 0; i < 8; i++) {
        put_dword(addr);
        puts("|");
        for (int j = 0; j < 8; j++) {
            put_byte(*(PBYTE)(addr+j));
            putc(' ');
        }
        puts("\b-");
        for (int j = 0; j < 8; j++) {
            put_byte(*(PBYTE)(addr+j+8));
            putc(' ');
        }
        puts("\b|");
        for (int j = 0; j < 16; j++) {
            putca(*(PBYTE)(addr+j));
        }
        puts("\n\r");
        addr += 16;
    }
}

static const char *key_seq(int k)
{
	switch (k) {
	case K_UP:    return "\033[A";
	case K_DOWN:  return "\033[B";
	case K_RIGHT: return "\033[C";
	case K_LEFT:  return "\033[D";
	case K_HOME:  return "\033[H";
	case K_END:   return "\033[F";
	case K_PGUP:  return "\033[5~";
	case K_PGDN:  return "\033[6~";
	case K_INS:   return "\033[2~";
	case K_DEL:   return "\033[3~";
	}
	return 0;
}

static const char *pend;

int getk() {
    for (;;) {
        int k;
        k = kbd_poll();
        if (k != K_NONE)return k;
    }
}

char getc() {
    for (;;) {
        int k;

        if (pend && *pend) {
            char c = *pend++;
            return c;
        }
        pend = 0;

        k = getk();
        if (k < 0x80)return (char)k;

        const char *s = key_seq(k);
        if (s) {
            pend = s+1;
            return s[0];
        }
    }
}