#include "convert.h"
#include "string.h"

const char _hex_digit[17]="0123456789ABCDEF";
BYTE hex_digit(char c) {
    if (c>='0' && c<='9')return (BYTE)(c-'0');
    if (c>='A' && c<='F')return (BYTE)(c-'A'+10);
    if (c>='a' && c<='f')return (BYTE)(c-'a'+10);
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
int hex_parse_w(PPCSTR p, PQWORD out) {
    PCSTR s = *p;
    QWORD v = 0;
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
int num_parse_w(PCSTR text, PQWORD out) {
    if (!text || !out)return 0;
    PCSTR start=text,end;
    skip_ws(&start);
    for (end=start;*end;end++);
    while (end>start && (end[-1]==' ' || end[-1]=='\t'))end--;
    int hexadecimal=end>start && (end[-1]=='h' || end[-1]=='H');
    if (hexadecimal)end--;
    if (end==start)return 0;
    QWORD value=0;
    for (PCSTR p=start;p<end;p++) {
        unsigned digit=hex_digit(*p);
        if (hexadecimal) {
            if (digit>15 || value>0x0fffffffffffffffULL)return 0;
            value=(value<<4)|digit;
        } else {
            if (digit>9 || value>1844674407370955161ULL ||
                (value==1844674407370955161ULL && digit>5))return 0;
            value=value*10+digit;
        }
    }
    *out=value;
    return 1;
}
int num_parse(PCSTR text, PDWORD out) {
    QWORD value;
    if (!out || !num_parse_w(text,&value) || value>0xffffffffULL)return 0;
    *out=(DWORD)value;
    return 1;
}
void hex_xlat(QWORD in, PSTR out, int digits) {
    int i=0;
    for (; i<digits; i++) {
        out[i]=_hex_digit[(in>>((digits-i-1)*4))&0xf];
    }
    out[digits]=0;
}
void bin_xlat(QWORD in, PSTR out, int digits) {
    int i=0;
    for (; i<digits; i++) {
        out[i]=_hex_digit[(in>>((digits-i-1)*1))&0x1];
    }
    out[digits]=0;
}
