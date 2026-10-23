#ifndef _COREUTIL_COMPAT_H
#define _COREUTIL_COMPAT_H
#include "stdio.h"
#include "crt.h"
#include "fs.h"
#include "string.h"
#include "mem.h"
#include "path.h"
#include "abi.h"

typedef struct PORT_FILE PORT_FILE;
extern PORT_FILE *port_stdin,*port_stdout,*port_stderr;
int port_run(int (*entry)(int,char **),int argc,char **argv);
void port_exit(int status) __attribute__((noreturn));
PORT_FILE *port_fopen(const char *path,const char *mode);
PORT_FILE *port_freopen(const char *path,const char *mode,PORT_FILE *stream);
int port_fclose(PORT_FILE *stream);
int port_getc(PORT_FILE *stream);
int port_ungetc(int ch,PORT_FILE *stream);
char *port_fgets(char *out,int size,PORT_FILE *stream);
int port_putc(int ch,PORT_FILE *stream);
size_t port_fwrite(const void *bytes,size_t size,size_t count,PORT_FILE *stream);
int port_fprintf(PORT_FILE *stream,const char *format,...);
void *port_malloc(size_t size);
void *port_zalloc(size_t size);
void *port_realloc(void *ptr,size_t size);
void port_free(void *ptr);
char *port_strdup(const char *text);
int port_memcmp(const void *a,const void *b,size_t n);
void *port_memchr(const void *bytes,int ch,size_t n);
void *port_memmove(void *out,const void *in,size_t n);
int port_atoi(const char *text);
int port_open(const char *path,int flags);
int port_creat(const char *path,int mode);
int port_read(int fd,void *out,unsigned count);
int port_write(int fd,const void *in,unsigned count);
int port_close(int fd);
int port_readline(void *unused,const char *prompt,char *line,int capacity);
void port_error(const char *format,...);
void port_perror(const char *text);
void port_printable(int ch,PORT_FILE *stream);
const char *port_skip_ws(const char *text);
extern unsigned option_mask32;
extern char bb_common_bufsiz1[1024];

#define FILE PORT_FILE
#define stdin port_stdin
#undef stdout
#define stdout port_stdout
#define stderr port_stderr
#define fopen port_fopen
#define freopen port_freopen
#define fclose port_fclose
#define getc port_getc
#define fgetc port_getc
#define ungetc port_ungetc
#define fgets port_fgets
#define putc port_putc
#define fwrite port_fwrite
#define fprintf port_fprintf
#define exit port_exit
#define malloc port_malloc
#define free port_free
#define xmalloc port_malloc
#define xzalloc port_zalloc
#define xrealloc port_realloc
#define xstrdup port_strdup
#define memcmp port_memcmp
#define memchr port_memchr
#define memmove port_memmove
#define atoi port_atoi
#define open port_open
#define creat port_creat
#define safe_read port_read
#define read port_read
#define full_write port_write
#define write port_write
#define close port_close
#define read_line_input port_readline
#define skip_whitespace port_skip_ws
#define bb_error_msg port_error
#define bb_simple_error_msg port_error
#define bb_simple_perror_msg port_perror
#define fputc_printable port_printable
#define fputs_stdout(s) port_fprintf(port_stdout,"%s",s)
#define MAIN_EXTERNALLY_VISIBLE
#define UNUSED_PARAM __attribute__((unused))
#define NOINLINE __attribute__((noinline))
#define SET_PTR_TO_GLOBALS(p) (ptr_to_globals=(p))
#define setup_common_bufsiz() ((void)0)
#define COMMON_BUFSIZE 1024
#define PRINTABLE_META 0
#define STDOUT_FILENO 1
#define EXIT_SUCCESS 0
#define TRUE 1
#define FALSE 0
typedef signed char smallint;
#ifndef NULL
#define NULL ((void *)0)
#endif
static inline int port_isdigit(int c) {return (unsigned)c-'0'<10;}
static inline int port_isblank(int c) {return c==' ' || c=='\t';}
static inline int port_isspace(int c) {return port_isblank(c) || (c>='\n' && c<='\r');}
static inline int port_isprint(int c) {return (unsigned)c-32<95;}
static inline int port_toascii(int c) {return c&127;}
#define isdigit port_isdigit
#define isblank port_isblank
#define isspace port_isspace
#define isprint port_isprint
#define toascii port_toascii
#endif
