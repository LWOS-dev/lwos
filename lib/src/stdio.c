#include "stdio.h"
#include "console.h"
#include "format.h"

/* Statically linked into each EXE: never a shared kernel/ABI global. */
struct _LW_FILE {
    PLW_BUF_TXT text;
    DWORD window_id, cursor_x, cursor_y;
    BYTE attr;
};
static FILE console;
FILE *stdout=&console;

int stdio_init(PCLW_ENV env) {
    console=(FILE){0};
    stdout=&console;
    if (!env || env->version!=LW_ENV_VERSION || env->size<sizeof(*env) ||
        env->vi.size<sizeof(LW_VI) || env->vi.mode!=LW_VI_TEXT || !env->vi.txt)
        return RM_ERR_ARGUMENT;
    PLW_BUF_TXT text=env->vi.txt;
    if (!text->buffer || !text->cols || !text->lines ||
        (QWORD)text->cols*text->lines>0x7fffffffu/sizeof(WORD))
        return RM_ERR_ARGUMENT;
    console.text=text;
    console.window_id=env->vi.window_id;
    console.attr=0x07;
    return RM_OK;
}
int stdio_set_cursor(DWORD x, DWORD y) {
    if (!console.text)return RM_ERR_NOT_READY;
    if (x>=console.text->cols || y>=console.text->lines)return RM_ERR_ARGUMENT;
    console.cursor_x=x;console.cursor_y=y;
    return RM_OK;
}
void stdio_get_cursor(PDWORD x, PDWORD y) {
    if (x)*x=console.cursor_x;
    if (y)*y=console.cursor_y;
}
void stdio_set_attr(BYTE attr) {console.attr=attr;}
static void scroll(FILE *stream) {
    PLW_BUF_TXT text=stream->text;
    if (stream->cursor_y<text->lines)return;
    volatile WORD *cells=text->buffer;
    DWORD count=text->cols*(text->lines-1);
    for (DWORD i=0;i<count;i++)cells[i]=cells[i+text->cols];
    for (DWORD i=count;i<count+text->cols;i++)cells[i]=((WORD)stream->attr<<8)|' ';
    stream->cursor_y=text->lines-1;
}
static void raw_char(FILE *stream, BYTE ch) {
    PLW_BUF_TXT text=stream->text;
    ((volatile WORD *)text->buffer)[stream->cursor_y*text->cols+stream->cursor_x]=
        ((WORD)stream->attr<<8)|ch;
    if (++stream->cursor_x==text->cols) {
        stream->cursor_x=0;stream->cursor_y++;scroll(stream);
    }
}
int fputc(int ch, FILE *stream) {
    if (stream!=&console || !stream->text)return EOF;
    BYTE c=(BYTE)ch;
    if (c=='\n') {stream->cursor_x=0;stream->cursor_y++;scroll(stream);}
    else if (c=='\r')stream->cursor_x=0;
    else if (c=='\t') {
        unsigned spaces=8-stream->cursor_x%8;
        while (spaces--)raw_char(stream,' ');
    } else if (c=='\b') {
        if (stream->cursor_x)stream->cursor_x--;
        else if (stream->cursor_y) {stream->cursor_y--;stream->cursor_x=stream->text->cols-1;}
    } else raw_char(stream,c);
    return c;
}
int putchar(int ch) {return fputc(ch,stdout);}
int fputs(const char *text, FILE *stream) {
    if (!text || stream!=&console || !stream->text)return EOF;
    int count=0;
    while (*text) {
        if (count==2147483647 || fputc((BYTE)*text++,stream)==EOF)return EOF;
        count++;
    }
    return count;
}
int puts(const char *text) {
    int count=fputs(text,stdout);
    if (count<0 || count==2147483647 || putchar('\n')==EOF)return EOF;
    return count+1;
}
static int console_write(int ch, void *stream) {return fputc(ch,stream);}
int vfprintf(FILE *stream, const char *format, va_list args) {
    if (stream!=&console || !stream->text)return EOF;
    return lw_vformat(console_write,stream,format,args);
}
int fprintf(FILE *stream, const char *format, ...) {
    va_list args;va_start(args,format);
    int rc=vfprintf(stream,format,args);
    va_end(args);return rc;
}
int vprintf(const char *format, va_list args) {return vfprintf(stdout,format,args);}
int printf(const char *format, ...) {
    va_list args;va_start(args,format);
    int rc=vprintf(format,args);
    va_end(args);return rc;
}
