#include "stdint.h"

PWORD txt_video=(PWORD)0xb8000;
BYTE txt_attr=0x07;
void putxyn(PCSTR str, int x, int y, int n) {
    for (int i=0; i<n; i++) {
        txt_video[x+y*80+i]=((WORD)txt_attr<<8)|(BYTE)str[i];
    }
}
void putxyc(BYTE c, int x, int y) {
    txt_video[x+y*80]=((WORD)txt_attr<<8)|c;
}
void putxy(const char *str, int x, int y) {
    for (int i=0; str[i]; i++) {
        txt_video[x+y*80+i]=((WORD)txt_attr<<8)|(BYTE)str[i];
    }
}
void set_attr(BYTE fore, BYTE back) {
    txt_attr=fore|(back<<4);
}
void screen_fill(BYTE back) {
    for (int i=0; i<80*25; i++) {
        txt_video[i]=back<<12;
    }
}