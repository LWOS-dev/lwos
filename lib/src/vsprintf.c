/* Port of lwos/v1b5/kernel/vsprintf.c: retains flag/width/precision parsing
 * and reverse-digit conversion. Replaces raw buffer writes with a sink,
 * corrects 32-bit varargs and INT_MIN handling, and makes snprintf bounded.
 * Unsupported conversions (including floating point and %n) return EOF.
 */
#include "stdio.h"
#include "format.h"

#define ZEROPAD 1
#define PLUS 4
#define SPACE 8
#define LEFT 16
#define SPECIAL 32
#define SMALL 64
#define INT_MAX_VALUE 2147483647
#define is_digit(c) ((c)>='0' && (c)<='9')

typedef struct {
    int (*write)(int,void *);
    void *context;
    int count;
} OUTPUT;
static int emit(OUTPUT *out, int ch) {
    if (out->count==INT_MAX_VALUE || out->write(ch,out->context)<0)return EOF;
    out->count++;
    return 0;
}
static int repeat(OUTPUT *out, int ch, int count) {
    while (count-->0)if (emit(out,ch)<0)return EOF;
    return 0;
}
static int skip_atoi(const char **s) {
    int value=0;
    while (is_digit(**s)) {
        int digit=*(*s)++-'0';
        if (value>(INT_MAX_VALUE-digit)/10)return EOF;
        value=value*10+digit;
    }
    return value;
}
static int number(OUTPUT *out, unsigned long long value, int base,
                  int width, int precision, int flags, int sign, int pointer) {
    const char *digits=(flags&SMALL) ? "0123456789abcdef" : "0123456789ABCDEF";
    char tmp[65];
    int n=0, prefix=0;
    unsigned long long original=value;
    if (value || precision!=0)do {
        tmp[n++]=digits[value%(unsigned)base];
        value/=(unsigned)base;
    } while (value);
    if (pointer || ((flags&SPECIAL) && base==16 && original))prefix=2;
    if ((flags&SPECIAL) && base==8 && (!n || tmp[n-1]!='0') && precision<=n)
        precision=n+1;
    if (precision>=0)flags&=~ZEROPAD;
    if (flags&LEFT)flags&=~ZEROPAD;
    int zeros=precision>n ? precision-n : 0;
    if (zeros>INT_MAX_VALUE-n-prefix-(sign!=0))return EOF;
    int total=n+zeros+prefix+(sign!=0);
    int pad=width>total ? width-total : 0;
    if (!(flags&(LEFT|ZEROPAD)) && repeat(out,' ',pad)<0)return EOF;
    if (sign && emit(out,sign)<0)return EOF;
    if (prefix && (emit(out,'0')<0 || emit(out,(flags&SMALL)?'x':'X')<0))return EOF;
    if ((flags&ZEROPAD) && repeat(out,'0',pad)<0)return EOF;
    if (repeat(out,'0',zeros)<0)return EOF;
    while (n)if (emit(out,tmp[--n])<0)return EOF;
    if ((flags&LEFT) && repeat(out,' ',pad)<0)return EOF;
    return 0;
}
int lw_vformat(int (*write)(int,void *), void *context, const char *fmt, va_list input) {
    if (!fmt || !write)return EOF;
    OUTPUT out={write,context,0};
    va_list args;
    va_copy(args,input);
    while (*fmt) {
        if (*fmt!='%') {if (emit(&out,(unsigned char)*fmt++)<0)goto error;continue;}
        fmt++;
        int flags=0;
        for (;;) {
            if (*fmt=='-')flags|=LEFT;
            else if (*fmt=='+')flags|=PLUS;
            else if (*fmt==' ')flags|=SPACE;
            else if (*fmt=='#')flags|=SPECIAL;
            else if (*fmt=='0')flags|=ZEROPAD;
            else break;
            fmt++;
        }
        int width=0,precision=-1;
        if (is_digit(*fmt)) {width=skip_atoi(&fmt);if (width<0)goto error;}
        else if (*fmt=='*') {
            fmt++; width=va_arg(args,int);
            if (width==(-INT_MAX_VALUE-1))goto error;
            if (width<0) {flags|=LEFT;width=-width;}
        }
        if (*fmt=='.') {
            fmt++;
            if (*fmt=='*') {fmt++;precision=va_arg(args,int);if (precision<0)precision=-1;}
            else {precision=skip_atoi(&fmt);if (precision<0)goto error;}
        }
        int length=0; /* -2 hh, -1 h, 0 int, 1 l, 2 ll */
        if (*fmt=='h') {fmt++;length=-1;if (*fmt=='h') {fmt++;length=-2;}}
        else if (*fmt=='l') {fmt++;length=1;if (*fmt=='l') {fmt++;length=2;}}
        char type=*fmt;
        if (!type)goto error;
        fmt++;
        if (type=='%' || type=='c' || type=='s') {
            if (length)goto error;
            if (type=='%') {if (emit(&out,'%')<0)goto error;continue;}
            char ch;
            const char *text;
            int size=0;
            if (type=='c') {ch=(char)va_arg(args,int);text=&ch;size=1;}
            else {
                text=va_arg(args,const char *);
                if (!text)text="(null)";
                while ((precision<0 || size<precision) && text[size]) {
                    if (size==INT_MAX_VALUE)goto error;
                    size++;
                }
            }
            int pad=width>size ? width-size : 0;
            if (!(flags&LEFT) && repeat(&out,' ',pad)<0)goto error;
            for (int i=0;i<size;i++)if (emit(&out,(unsigned char)text[i])<0)goto error;
            if ((flags&LEFT) && repeat(&out,' ',pad)<0)goto error;
            continue;
        }
        if (type!='d' && type!='i' && type!='u' && type!='x' && type!='X' &&
            type!='o' && type!='p')goto error;
        unsigned long long value;
        int sign=0,base=10;
        if (type=='d' || type=='i') {
            long long signed_value;
            if (length==2)signed_value=va_arg(args,long long);
            else if (length==1)signed_value=va_arg(args,long);
            else {
                signed_value=va_arg(args,int);
                if (length==-1)signed_value=(short)signed_value;
                if (length==-2)signed_value=(signed char)signed_value;
            }
            value=(unsigned long long)signed_value;
            if (signed_value<0) {sign='-';value=0-value;}
            else if (flags&PLUS)sign='+';
            else if (flags&SPACE)sign=' ';
        } else if (type=='p') {
            if (length)goto error;
            value=(__UINTPTR_TYPE__)va_arg(args,void *);
            flags|=SMALL;
            precision=(int)(sizeof(void *)*2);
        } else {
            if (length==2)value=va_arg(args,unsigned long long);
            else if (length==1)value=va_arg(args,unsigned long);
            else {
                value=va_arg(args,unsigned int);
                if (length==-1)value=(unsigned short)value;
                if (length==-2)value=(unsigned char)value;
            }
        }
        if (type=='o')base=8;
        if (type=='x' || type=='X' || type=='p')base=16;
        if (type=='x')flags|=SMALL;
        if (number(&out,value,base,width,precision,flags,sign,type=='p')<0)goto error;
    }
    va_end(args);
    return out.count;
error:
    va_end(args);
    return EOF;
}

typedef struct {char *data;size_t size,used;} BUFFER;
static int buffer_write(int ch, void *context) {
    BUFFER *buf=context;
    if (buf->size && buf->used<buf->size-1)buf->data[buf->used]=(char)ch;
    buf->used++;
    return ch;
}
int vsnprintf(char *buffer, size_t size, const char *format, va_list args) {
    if (size && !buffer)return EOF;
    BUFFER out={buffer,size,0};
    int rc=lw_vformat(buffer_write,&out,format,args);
    if (size)buffer[out.used<size ? out.used : size-1]=0;
    return rc;
}
int snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list args;va_start(args,format);
    int rc=vsnprintf(buffer,size,format,args);
    va_end(args);return rc;
}
int vsprintf(char *buffer, const char *format, va_list args) {
    if (!buffer)return EOF;
    return vsnprintf(buffer,(size_t)-1,format,args);
}
int sprintf(char *buffer, const char *format, ...) {
    va_list args;va_start(args,format);
    int rc=vsprintf(buffer,format,args);
    va_end(args);return rc;
}
