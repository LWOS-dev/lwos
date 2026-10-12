#include "convert.h"
#include "string.h"

const char _hex_digit[17]="0123456789ABCDEF";
BYTE hex_digit(char c) {
    char v=c;
    if ((v&0xf0)==0x30)return v&0xf;
    switch (v&0x60) {
        case 0x40:case 0x60:v+=9;v&=0xf;return v;
        default:return 0xff;
    }
    return 0xff;
}
int hex_parse(PPCSTR p, PDWORD out) {
    PCSTR s = *p;
    DWORD v = 0;
    int digits = 0;
    skip_ws(&s);
    for (;;) {
        unsigned int d = hex_digit(*s);
        if (d==0xff)break;
        v=v<<4;
        v|=(DWORD)(d&0xf);
        digits++;
        s++;
    }
    if (!digits)return 0;
    *p=s;
    *out=v;
    return 1;
}
void hex_xlat(QWORD in, PSTR out, int digits) {
    int i=0;
    for (; i<digits; i++) {
        out[i]=_hex_digit[(in>>((digits-i-1)*4))&0xf];
    }
    out[++i]=0;
}
