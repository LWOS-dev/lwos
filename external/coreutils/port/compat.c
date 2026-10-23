#include "compat.h"

#define STREAMS 16
struct PORT_FILE {
    FS_FILE file;
    int active,terminal,writing,pushed;
    unsigned pos,end;
    BYTE buffer[512];
};
static PORT_FILE streams[STREAMS];
PORT_FILE *port_stdin=&streams[0],*port_stdout=&streams[1],*port_stderr=&streams[2];
static FS_VOLUME volume;
static int mounted,io_error,last_error,status;
static void *return_point[5];
static char terminal_line[1024];
static unsigned terminal_pos,terminal_end;
unsigned option_mask32;
char bb_common_bufsiz1[1024];

typedef struct BLOCK {struct BLOCK *next,*prev;size_t size;} BLOCK;
static BLOCK *blocks;
void *port_malloc(size_t size) {
    if (size>0x7ffff000u-sizeof(BLOCK))port_exit(2);
    BLOCK *b=rm_alloc(sizeof(*b)+(size ? size : 1),0,0);
    if (!b) {printf("out of memory\n");port_exit(2);}
    b->prev=0;b->next=blocks;b->size=size;
    if (blocks)blocks->prev=b;
    blocks=b;return b+1;
}
void port_free(void *ptr) {
    if (!ptr)return;
    BLOCK *b=(BLOCK *)ptr-1;
    if (b->prev)b->prev->next=b->next;else blocks=b->next;
    if (b->next)b->next->prev=b->prev;
    rm_free(b);
}
void *port_zalloc(size_t size) {void *p=port_malloc(size);memzero(p,size);return p;}
void *port_realloc(void *ptr,size_t size) {
    void *p=port_malloc(size);
    if (ptr) {size_t n=((BLOCK *)ptr-1)->size;memcpy(p,ptr,n<size ? n : size);port_free(ptr);}
    return p;
}
char *port_strdup(const char *s) {char *p=port_malloc(strlen(s)+1);strcpy(p,s);return p;}
int port_memcmp(const void *a,const void *b,size_t n) {
    const BYTE *x=a,*y=b;
    while (n--) {if (*x!=*y)return *x-*y;x++;y++;}return 0;
}
void *port_memchr(const void *bytes,int ch,size_t n) {
    const BYTE *p=bytes;while (n--) {if (*p==(BYTE)ch)return (void *)p;p++;}return 0;
}
void *port_memmove(void *out,const void *in,size_t n) {
    BYTE *d=out;const BYTE *s=in;
    if (d<s) {for (size_t i=0;i<n;i++)d[i]=s[i];}
    else {while (n) {n--;d[n]=s[n];}}return out;
}
int port_atoi(const char *s) {
    int n=0;
    while (isdigit(*s)) {
        if (n>214748364 || (n==214748364 && *s>'7'))port_exit(2);
        n=n*10+*s++-'0';
    }
    return n;
}
const char *port_skip_ws(const char *s) {while (isspace(*s))s++;return s;}

void port_exit(int code) {status=code;__builtin_longjmp(return_point,1);}
int port_run(int (*entry)(int,char **),int argc,char **argv) {
    memzero(streams,sizeof(streams));memzero(&volume,sizeof(volume));
    mounted=io_error=last_error=status=0;blocks=0;option_mask32=0;
    terminal_pos=terminal_end=0;
    for (int i=0;i<STREAMS;i++)streams[i].pushed=-1;
    for (int i=0;i<3;i++) {streams[i].active=1;streams[i].terminal=1;streams[i].writing=i!=0;}
    if (!__builtin_setjmp(return_point))status=entry(argc,argv);
    for (int i=0;i<STREAMS;i++)if (streams[i].active && !streams[i].terminal)port_fclose(&streams[i]);
    while (blocks)port_free(blocks+1);
    return status ? status : (io_error ? 2 : 0);
}

static int keyboard(void) {
    for (;;) {
        LW_WINDOW_EVENT event;
        int rc=rm_window_poll(lw_env->vi.window_id,&event);
        if (rc<0)return EOF;
        if (rc==1) {if (event.type==LW_WINDOW_KEY)return event.value;continue;}
        if (rm_window_wait(lw_env->vi.window_id)!=RM_OK)return EOF;
    }
}
int port_readline(void *unused,const char *prompt_text,char *out,int capacity) {
    (void)unused;int n=0,overflow=0;
    if (prompt_text)printf("%s",prompt_text);
    for (;;) {
        int c=keyboard();
        if (c==EOF || c==4 || c==VK_ESC) {out[n]=0;return n ? n : -1;}
        if (c==3) {out[0]=0;printf("^C\n");return 0;}
        if (c==VK_ENTER) {
            putchar('\n');
            if (overflow) {port_error("input line too long");out[0]=0;return 0;}
            out[n++]='\n';out[n]=0;return n;
        }
        if (c==VK_BKSPACE) {if (n) {n--;printf("\b \b");}continue;}
        if (c=='\t' || (c>=32 && c<127)) {
            if (n>=capacity-2) {overflow=1;continue;}
            out[n++]=c;putchar(c);
        }
    }
}
static int prepare(PORT_FILE *f,const char *name,const char *mode) {
    char path[256];int rc=lw_path_resolve(lw_env_cwd(lw_env),name,path,sizeof(path));
    if (!rc && !mounted) {rc=fs_mount(&volume,lw_blk_get(0));if (!rc)mounted=1;}
    if (rc) {last_error=rc;return -1;}
    int writing=*mode=='w' || *mode=='a';
    rc=fs_file_open(&volume,path,&f->file);
    if (rc==FS_ERR_NOT_FOUND && writing) {
        rc=fs_touch(&volume,path);
        if (!rc)rc=fs_file_open(&volume,path,&f->file);
    }
    if (!rc && *mode=='w')rc=fs_file_truncate(&f->file);
    if (!rc && *mode=='a')rc=fs_file_seek(&f->file,0,FS_SEEK_END);
    if (rc) {fs_file_close(&f->file);last_error=rc;return -1;}
    f->active=1;f->terminal=0;f->writing=writing;f->pushed=-1;f->pos=f->end=0;return 0;
}
PORT_FILE *port_fopen(const char *name,const char *mode) {
    if (!strcmp(name,"/dev/stdout"))return port_stdout;
    if (!strcmp(name,"/dev/stderr"))return port_stderr;
    for (int i=3;i<STREAMS;i++)if (!streams[i].active)
        return prepare(&streams[i],name,mode) ? 0 : &streams[i];
    last_error=FS_ERR_RANGE;return 0;
}
PORT_FILE *port_freopen(const char *name,const char *mode,PORT_FILE *f) {
    port_fclose(f);return prepare(f,name,mode) ? 0 : f;
}
static int flush_stream(PORT_FILE *f) {
    if (!f->writing || !f->end)return 0;
    DWORD n=0;int rc=fs_file_write(&f->file,f->buffer,f->end,&n);
    if (rc || n!=f->end) {io_error=1;last_error=rc ? rc : FS_ERR_IO;f->end=0;return -1;}
    f->end=0;return 0;
}
int port_fclose(PORT_FILE *f) {
    if (!f || !f->active || f->terminal)return 0;
    int rc=flush_stream(f);
    fs_file_close(&f->file);f->active=0;return rc;
}
int port_getc(PORT_FILE *f) {
    if (!f || !f->active || f->writing)return EOF;
    if (f->pushed!=-1) {int c=f->pushed;f->pushed=-1;return c;}
    if (f->terminal) {
        while (terminal_pos==terminal_end) {
            int n=port_readline(0,"",terminal_line,sizeof(terminal_line));
            if (n<0)return EOF;
            terminal_pos=0;terminal_end=n;
        }
        return (BYTE)terminal_line[terminal_pos++];
    }
    if (f->pos==f->end) {
        DWORD n=0;int rc=fs_file_read(&f->file,f->buffer,sizeof(f->buffer),&n);
        if (rc) {io_error=1;last_error=rc;port_error("read error %d",rc);port_exit(2);}
        f->pos=0;f->end=n;if (!n)return EOF;
    }
    return f->buffer[f->pos++];
}
int port_ungetc(int ch,PORT_FILE *f) {if (ch==EOF || f->pushed!=-1)return EOF;return f->pushed=(BYTE)ch;}
char *port_fgets(char *out,int size,PORT_FILE *f) {
    int n=0,c;
    while (n<size-1 && (c=port_getc(f))!=EOF) {out[n++]=c;if (c=='\n')break;}
    out[n]=0;return n ? out : 0;
}
int port_putc(int c,PORT_FILE *f) {
    if (!f || !f->active || !f->writing) {io_error=1;return EOF;}
    if (f->terminal) {int rc=putchar(c);if (rc==EOF)io_error=1;return rc;}
    f->buffer[f->end++]=c;
    if (f->end==sizeof(f->buffer) && flush_stream(f))return EOF;
    return (BYTE)c;
}
size_t port_fwrite(const void *bytes,size_t size,size_t count,PORT_FILE *f) {
    if (!size || count>0x7fffffff/size)return 0;
    const BYTE *p=bytes;size_t n=size*count,i=0;
    for (;i<n;i++)if (port_putc(p[i],f)==EOF)break;
    return i/size;
}
int port_fprintf(PORT_FILE *f,const char *format,...) {
    char buffer[4096];va_list args;va_start(args,format);
    int n=vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    if (n<0 || n>=(int)sizeof(buffer)) {io_error=1;return EOF;}
    return port_fwrite(buffer,1,n,f)==(unsigned)n ? n : EOF;
}
void port_error(const char *format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');
}
void port_perror(const char *text) {port_error("%s: error %d",text,last_error);}
void port_printable(int c,PORT_FILE *f) {
    if ((BYTE)c<32) {port_putc('^',f);port_putc((BYTE)c+'@',f);}
    else if ((BYTE)c==127)port_fprintf(f,"^?");else port_putc(c,f);
}
int port_open(const char *path,int flags) {(void)flags;PORT_FILE *f=port_fopen(path,"r");return f ? f-streams : -1;}
int port_creat(const char *path,int mode) {(void)mode;PORT_FILE *f=port_fopen(path,"w");return f ? f-streams : -1;}
int port_read(int fd,void *out,unsigned count) {
    if (fd<0 || fd>=STREAMS)return -1;
    BYTE *p=out;unsigned n=0;
    for (;n<count;n++) {int c=port_getc(&streams[fd]);if (c==EOF)break;p[n]=c;}
    return n;
}
int port_write(int fd,const void *in,unsigned count) {
    if (fd<0 || fd>=STREAMS)return -1;
    return port_fwrite(in,1,count,&streams[fd]);
}
int port_close(int fd) {return fd<0 || fd>=STREAMS ? -1 : port_fclose(&streams[fd]);}
