#ifndef _LW_STDIO_H
#define _LW_STDIO_H
#include "stdarg.h"
/* Matches the compiler ABI, including hosted regression builds. */
typedef __SIZE_TYPE__ size_t;
#define EOF (-1)
typedef struct _LW_FILE FILE;
extern FILE *stdout;
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsprintf(char *buffer, const char *format, va_list args);
int sprintf(char *buffer, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list args);
int printf(const char *format, ...);
int fputc(int ch, FILE *stream);
int putchar(int ch);
int fputs(const char *text, FILE *stream);
int puts(const char *text);
#endif
