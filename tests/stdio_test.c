#include <assert.h>
#include "stdio.h"
#include "console.h"
/* Compare to the host libc without interposing its stdio symbols. */
extern int host_vsnprintf(char *,size_t,const char *,va_list) __asm__("vsnprintf");
extern int second_stdio_init(PCLW_ENV);
extern int second_printf(const char *,...);
extern void second_stdio_get_cursor(PDWORD,PDWORD);
static void equal(const char *a,const char *b) {
    for (size_t i=0;;i++) {assert(a[i]==b[i]);if (!a[i])break;}
}
static void compare(const char *format,...) {
    char actual[256],expected[256];
    va_list a,b;
    va_start(a,format);va_copy(b,a);
    int n=vsnprintf(actual,sizeof(actual),format,a);
    int m=host_vsnprintf(expected,sizeof(expected),format,b);
    va_end(b);va_end(a);
    assert(n==m);equal(actual,expected);
}
static LW_ENV environment(LW_BUF_TXT *text,DWORD id) {
    LW_ENV env={0};
    env.version=LW_ENV_VERSION;env.size=sizeof(env);
    env.vi.size=sizeof(env.vi);env.vi.mode=LW_VI_TEXT;
    env.vi.window_id=id;env.vi.txt=text;
    return env;
}
static void blank(WORD *cells,unsigned count) {
    for (unsigned i=0;i<count;i++)cells[i]=0x0720;
}
int main(void) {
    compare("%d %i %u %x %X %o %c %% %s",-2147483647-1,-123,4294967295u,0x12ab,0xab12,077u,'Q',"text");
    compare("[%08d][%-8d][%+8d][% d]",-42,42,42,42);
    compare("%#08x %#o %#.0o %.0u %.0x",0x2au,8u,0u,0u,0u);
    compare("[%*.*s][%.*d][%*u]",-8,3,"abcdef",6,12,7,42u);
    compare("%hhd %hhu %hd %hu %ld %lu %lld %llu",-128,255u,-32768,65535u,-1234L,1234UL,
            -9223372036854775807LL-1,18446744073709551615ULL);
    compare("%.*s %.*d",-1,"abc",-1,42);
    char buffer[64];
    assert(snprintf(buffer,sizeof(buffer),"%p",(void *)0x1234)==(int)(2+sizeof(void *)*2));
    equal(buffer,sizeof(void *)==4 ? "0x00001234" : "0x0000000000001234");
    assert(sprintf(buffer,"%s:%d","value",7)==7);equal(buffer,"value:7");
    assert(snprintf(buffer,sizeof(buffer),"%s",(char *)0)==6);equal(buffer,"(null)");
    struct {char before;char data[5];char after;} bounded={'L',{0},'R'};
    assert(snprintf(bounded.data,5,"abcdef%d",12)==8);
    equal(bounded.data,"abcd");assert(bounded.before=='L' && bounded.after=='R');
    bounded.data[0]='Z';assert(snprintf(bounded.data,0,"abcdef")==6 && bounded.data[0]=='Z');
    assert(snprintf(0,0,"%d",123)==3);
    assert(snprintf(bounded.data,1,"abcdef")==6 && !bounded.data[0]);
    assert(snprintf(0,1,"x")==EOF);
    int untouched=99;
    assert(snprintf(buffer,sizeof(buffer),"%n",&untouched)==EOF && untouched==99);
    assert(snprintf(buffer,sizeof(buffer),"%f",1.5)==EOF);
    assert(snprintf(buffer,sizeof(buffer),"%ls",(void *)0)==EOF);
    assert(snprintf(buffer,sizeof(buffer),"bad %")==EOF);equal(buffer,"bad ");
    assert(snprintf(buffer,sizeof(buffer),"%2147483648d",1)==EOF);
    assert(snprintf(buffer,sizeof(buffer),"%*d",-2147483647-1,1)==EOF);
    assert(printf("unbound")==EOF && putchar('x')==EOF);

    WORD cells[2002],other[32];blank(cells,2002);blank(other,32);
    cells[2000]=0x1234;cells[2001]=0x5678;
    LW_BUF_TXT txt={80,25,cells,0},txt2={8,4,other,0};
    LW_ENV env=environment(&txt,11),env2=environment(&txt2,12);
    assert(stdio_init(&env)==RM_OK && second_stdio_init(&env2)==RM_OK);
    assert(printf("first")==5 && second_printf("AB\nC")==4);
    DWORD x,y,x2,y2;
    stdio_get_cursor(&x,&y);second_stdio_get_cursor(&x2,&y2);
    assert(x==5 && y==0 && x2==1 && y2==1);
    assert(printf("!")==1 && second_printf("D")==1);
    assert((cells[5]&255)=='!' && (other[9]&255)=='D' && (other[1]&255)=='B');
    assert(stdio_set_cursor(80,0)==RM_ERR_ARGUMENT);
    stdio_set_cursor(0,0);stdio_set_attr(0x1e);
    assert(printf("a\tb\rZ\n")==6);
    assert(cells[0]==0x1e5a && cells[8]==0x1e62);
    stdio_get_cursor(&x,&y);assert(x==0 && y==1);
    /* A foreground child joins this same terminal, then returns to the parent. */
    assert(second_stdio_init(&env)==RM_OK);
    assert(second_printf("child\n")==6);
    stdio_get_cursor(&x,&y);assert(x==0 && y==2);
    assert(printf("parent")==6);
    assert((cells[80]&255)=='c' && (cells[160]&255)=='p');
    second_stdio_get_cursor(&x,&y);assert(x==6 && y==2);
    char long_text[1401];for (int i=0;i<1400;i++)long_text[i]='q';long_text[1400]=0;
    stdio_set_cursor(0,0);assert(printf("%s",long_text)==1400);
    stdio_get_cursor(&x,&y);assert(x==40 && y==17);
    assert(cells[1399]==0x1e71 && cells[2000]==0x1234 && cells[2001]==0x5678);
    assert(puts("ok")==3);

    WORD small[10];blank(small,10);small[8]=0xbeef;
    LW_BUF_TXT tiny={4,2,small,0};LW_ENV tiny_env=environment(&tiny,13);
    assert(stdio_init(&tiny_env)==RM_OK);
    assert(printf("abcdefgh")==8);
    for (int i=0;i<4;i++)assert(small[i]==(0x0700|('e'+i)) && small[i+4]==0x0720);
    stdio_get_cursor(&x,&y);assert(x==0 && y==1);
    stdio_set_attr(0x2f);printf("Z\n");
    assert(small[0]==0x2f5a && small[4]==0x2f20 && small[8]==0xbeef);
    assert(stdio_init(0)==RM_ERR_ARGUMENT && printf("bad")==EOF);
    return 0;
}
