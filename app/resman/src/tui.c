#include "../include/tui.h"

/* RESMAN 自己的文本输出状态，与 MONITOR 中的副本相互独立。 */
PWORD video = (PWORD)0xb8000;
BYTE vi_attr = 0x07;

void putxy(const char *str, int x, int y)
{
    for (int i = 0; str[i]; i++) {
        video[x + y * 80 + i] = ((WORD)vi_attr << 8) | (BYTE)str[i];
    }
}

/* 输出恰好 n 个字符，调用方保证缓冲区有 n 个可读字节。 */
void putxyn(const char *str, int x, int y, int n)
{
    for (int i = 0; i < n; i++) {
        video[x + y * 80 + i] = ((WORD)vi_attr << 8) | (BYTE)str[i];
    }
}

void putxyc(BYTE c, int x, int y)
{
    video[x + y * 80] = ((WORD)vi_attr << 8) | c;
}

void set_attr(BYTE fore, BYTE back) {
    vi_attr = fore | (back << 4);
}

void screen_fill(BYTE back) {
    for (int i=0; i<80*25; i++) {
        video[i]=back<<12;
    }
}