/* Actual command/parser/editor with scripted terminal and fallible FAT I/O. */
#include <assert.h>
#define main hed_program_main
#define printf hed_printf
#define putchar hed_putchar
#include "../external/coreutils/hed.c"
#undef main
#undef printf
#undef putchar
#undef free
extern void *calloc(size_t,size_t);
extern void free(void *);
PVOID *lw_abi_base;
PVOID *rm_base;
PCLW_ENV lw_env;
static PVOID abi[LW_SLOT_COUNT],rm[LW_RM_SLOT_COUNT];
static LW_ENV env;
static BLKDEV disk;
static BYTE file_bytes[4096];
static DWORD file_length,file_pos,allocated;
static int exists,fail_alloc,fail_read,fail_write,fail_close,zero_read,zero_write;
static const char **input;
static char output[24000];static unsigned used;
int hed_printf(const char *fmt,...) {
    va_list args;va_start(args,fmt);
    int n=vsnprintf(output+used,sizeof(output)-used,fmt,args);va_end(args);
    assert(n>=0 && (unsigned)n<sizeof(output)-used);used+=n;return n;
}
int hed_putchar(int c) {assert(used<sizeof(output)-1);output[used++]=c;output[used]=0;return c;}
int port_run(int (*entry)(int,char **),int argc,char **argv) {return entry(argc,argv);}
int port_readline(void *unused,const char *prompt,char *out,int capacity) {
    (void)unused;(void)prompt;
    if (!input || !*input)return -1;
    const char *s=*input++;unsigned n=0;
    while (*s) {assert(n<(unsigned)capacity-2);out[n++]=*s++;}
    out[n++]='\n';out[n]=0;return n;
}
PVOID memcpy(PVOID out,PCVOID in,int n) {
    for (int i=0;i<n;i++)((PBYTE)out)[i]=((PCBYTE)in)[i];
    return out;
}
PVOID memzero(PVOID out,int n) {for (int i=0;i<n;i++)((PBYTE)out)[i]=0;return out;}
static PVOID alloc(DWORD n,DWORD alignment,DWORD tag) {
    (void)alignment;(void)tag;
    if (fail_alloc)return 0;
    void *p=calloc(1,n);assert(p);allocated++;return p;
}
static int dealloc(PVOID p) {assert(p && allocated);allocated--;free(p);return 0;}
static PBLKDEV block(DWORD index) {assert(!index);return &disk;}
int fs_mount(PFS_VOLUME v,PBLKDEV d) {assert(d==&disk);v->mounted=1;return 0;}
int fs_file_open(PFS_VOLUME v,PCSTR p,PFS_FILE f) {
    (void)f;assert(v->mounted && !strcmp(p,"/BIN/DATA.BIN"));file_pos=0;
    return exists ? 0 : FS_ERR_NOT_FOUND;
}
int fs_touch(PFS_VOLUME v,PCSTR p) {(void)v;(void)p;exists=1;file_length=0;return 0;}
int fs_file_size(PFS_FILE f,PDWORD n) {(void)f;*n=file_length;return 0;}
int fs_file_truncate(PFS_FILE f) {(void)f;file_pos=file_length=0;return 0;}
int fs_file_read(PFS_FILE f,PVOID out,DWORD capacity,PDWORD n) {
    (void)f;*n=0;if (fail_read)return FS_ERR_IO;if (zero_read)return 0;
    *n=file_length-file_pos;if (*n>capacity)*n=capacity;if (*n>3)*n=3;
    memcpy(out,file_bytes+file_pos,*n);file_pos+=*n;return 0;
}
int fs_file_write(PFS_FILE f,PCVOID bytes,DWORD size,PDWORD n) {
    (void)f;*n=0;if (fail_write)return FS_ERR_IO;if (zero_write)return 0;
    *n=size>3 ? 3 : size;assert(file_pos+*n<=sizeof(file_bytes));
    memcpy(file_bytes+file_pos,bytes,*n);file_pos+=*n;file_length=file_pos;return 0;
}
int fs_file_close(PFS_FILE f) {(void)f;return fail_close ? FS_ERR_IO : 0;}
static int cmd(HED *e,const char *s) {
    char line[1024];strcpy(line,s);return command(e,line);
}
static void bytes(HED *e,const BYTE *expected,DWORD n) {
    assert(e->length==n);
    for (DWORD i=0;i<n;i++)assert(e->data[i]==expected[i]);
}
static void reset(void) {
    assert(!allocated);used=0;output[0]=0;exists=0;file_length=file_pos=0;
    fail_alloc=fail_read=fail_write=fail_close=zero_read=zero_write=0;input=0;
}
int main(void) {
    abi[LW_SLOT_BLK_GET]=(PVOID)block;lw_abi_base=abi;
    rm[LW_RM_SLOT_ALLOC]=(PVOID)alloc;rm[LW_RM_SLOT_FREE]=(PVOID)dealloc;
    rm_base=rm;
    env.size=sizeof(env);env.rm=rm;strcpy(env.cwd,"/BIN");lw_env=&env;
    reset();HED e={0};
    assert(!cmd(&e,"a 00 ff 0a 4142"));
    bytes(&e,(BYTE[]){0,255,10,65,66},5);
    assert(!cmd(&e,"0,$ p") && e.cursor==4);
    assert(!cmd(&e,"0x1 o 7f 00"));bytes(&e,(BYTE[]){0,127,0,65,66},5);
    assert(!cmd(&e,"u"));bytes(&e,(BYTE[]){0,255,10,65,66},5);
    assert(!cmd(&e,"u"));bytes(&e,(BYTE[]){0,127,0,65,66},5);
    assert(!cmd(&e,"0 i aa"));bytes(&e,(BYTE[]){170,0,127,0,65,66},6);
    assert(!cmd(&e,"1,3 c 11 22"));bytes(&e,(BYTE[]){170,17,34,65,66},5);
    assert(!cmd(&e,"$ a cc"));bytes(&e,(BYTE[]){170,17,34,65,66,204},6);
    const char *bad[]={"0 o abc","0 o gg","0 o","0 o 00112233445566",
        "5,2 d","0,99 d","4294967296 p","0x100000000 p","0,$ o ff","u junk","Q junk"};
    BYTE *data=e.data,*old_undo=e.undo;DWORD cursor=e.cursor;
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        assert(!cmd(&e,bad[i]));assert(e.data==data && e.undo==old_undo && e.cursor==cursor);
        bytes(&e,(BYTE[]){170,17,34,65,66,204},6);
    }
    fail_alloc=1;assert(!cmd(&e,"0 i ff") && e.data==data && e.undo==old_undo);fail_alloc=0;
    const char *multi[]={"00ff","0a 7f",".",0};input=multi;
    assert(!cmd(&e,"0,1 c"));bytes(&e,(BYTE[]){0,255,10,127,34,65,66,204},8);
    const char *invalid[]={"00","zz",0};input=invalid;data=e.data;
    assert(!cmd(&e,"0 i") && e.data==data);input=0;
    assert(!cmd(&e,"0 i") && e.data==data); /* EOF cancels pending input. */
    assert(!cmd(&e,"q") && cmd(&e,"Q"));
    assert(!save(&e,"DATA.BIN") && !e.dirty && exists && file_length==8);
    assert(!cmd(&e,"u") && e.dirty);assert(!cmd(&e,"u") && !e.dirty);
    assert(!cmd(&e,"0 o ee"));fail_write=1;
    assert(save(&e,"")==FS_ERR_IO && e.dirty && e.data[0]==238);fail_write=0;
    zero_write=1;assert(save(&e,"")==FS_ERR_IO && e.dirty);zero_write=0;
    fail_close=1;assert(save(&e,"")==FS_ERR_IO && e.dirty);fail_close=0;
    assert(!save(&e,"") && !e.dirty);
    assert(!cmd(&e,"% d") && !e.length);
    assert(!save(&e,"") && !file_length && cmd(&e,"q"));cleanup(&e);
    reset();exists=1;file_length=5;memcpy(file_bytes,(BYTE[]){0,10,255,0,127},5);
    e=(HED){0};assert(!load(&e,"DATA.BIN"));bytes(&e,file_bytes,5);cleanup(&e);
    e=(HED){0};fail_read=1;assert(load(&e,"DATA.BIN")==FS_ERR_IO && !e.data);fail_read=0;
    zero_read=1;assert(load(&e,"DATA.BIN")==FS_ERR_IO && !e.data);zero_read=0;
    file_length=HED_MAX+1;assert(load(&e,"DATA.BIN")==FS_ERR_RANGE);
    reset();
    const char *script[]={"a","00 ff 0a",".","q","w DATA.BIN","q",0};input=script;
    char *args[]={"hed",0};assert(!hed_main(1,args));
    assert(exists && file_length==3 && file_bytes[0]==0 && file_bytes[1]==255 && file_bytes[2]==10);
    reset();const char *eof[]={"a 01",0};input=eof;
    assert(hed_main(1,args)==1 && !exists);
    reset();return 0;
}
