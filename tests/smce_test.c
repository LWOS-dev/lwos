/* AT&T two-pass and encoding regressions, added with AI assistance. */
#include <assert.h>
#define main smce_main
#include "../external/smce.c"
#undef main

static void reset(void) {memzero(sym_table,sizeof(sym_table));sys_pos=0;}
static ENCODING encode(PCSTR source) {
    char text[1024];ENCODING e;
    reset();strcpy(text,source);
    assert(count_line(text)==0);
    DWORD size=sys_pos;sys_pos=0;
    strcpy(text,source);assert(encode_line(text,2,&e)==0 && e.size==size);
    return e;
}
static void check(PCSTR source,PCBYTE expected,unsigned size) {
    ENCODING e=encode(source);
    assert(e.size==size);
    for (unsigned i=0;i<size;i++)assert(e.bytes[i]==expected[i]);
}
int main(int argc,char **argv) {
    if (argc==2) {
        ENCODING e=encode(argv[1]);
        for (DWORD i=0;i<e.size;i++)printf("%02x",e.bytes[i]);
        printf("\n");return 0;
    }
    const BYTE imm[]={0xb8,0x78,0x56,0x34,0x12};
    check("movl $0x12345678, %eax # immediate",imm,sizeof(imm));
    const BYTE reg[]={0x89,0xc3};check("mov %eax,%ebx",reg,sizeof(reg));
    const BYTE mem[]={0x89,0x44,0x8b,0xf0};
    check("movl %eax, -16(%ebx, %ecx, 4)",mem,sizeof(mem));
    const BYTE word[]={0x66,0xb9,0xff,0xff};check("movw $-1,%cx",word,sizeof(word));
    const BYTE data[]={1,2,255};check(".byte 1, 02, 0xff",data,sizeof(data));
    const char *bad[]={"mov", "movl %eax", "movl $1,", "movl ,%eax", "movl $1,%ax",
        "movb %eax,%ebx", "movl %eax,$1", "movl (%eax),(%ebx)", "mov $1,(%eax)",
        "movl $0x100000000,%eax", "movb $256,%al", "movw $65536,%ax", "movb $-129,%al",
        "movl %eax,(%bx)", "movl %eax,(%ebx,%esp,2)", "movl %eax,(%ebx,%ecx,3)",
        "movl %eax,()", "movl %eax,(%ebx,)", "movl %eax,(%ebx,%ecx,)",
        "movl %eax,(%ebx,%ecx,4,2)", "movl %eax,(%ebx", "movl %eax,(%ebx))",
        "movl %eax,(%ebx)garbage", "movl %eax,%ebx, %ecx", "nop %eax",
        ".long", ".word 0xgg", ".byte 1,", ".org missing", ".org -1",
        "db 1", "label name", "jmps name", "123: nop", "unknown %eax"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        char text[1024];ENCODING e;reset();strcpy(text,bad[i]);
        assert(count_line(text)!=0);
        strcpy(text,bad[i]);assert(encode_line(text,2,&e)!=0);
    }
    char program[][128]={"# comment", ".org 16", "start: movl $target,%eax",
        "movl target(%ebx),%ecx", "jmp start", "target: .long start", "nop"};
    reset();
    for (unsigned i=0;i<sizeof(program)/sizeof(program[0]);i++) {
        char text[128];strcpy(text,program[i]);assert(count_line(text)==0);
    }
    DWORD total=sys_pos;
    assert(sym_table[find_symbol_name("start")].abs_off==16);
    assert(sym_table[find_symbol_name("target")].abs_off==32);
    sys_pos=0;
    for (unsigned i=0;i<sizeof(program)/sizeof(program[0]);i++) {
        char text[128];ENCODING e;strcpy(text,program[i]);assert(encode_line(text,2,&e)==0);
        if (i==1)assert(e.fill && e.size==16);
        if (i==2)assert(e.bytes[1]==32);
        if (i==3)assert(e.size==6 && e.bytes[2]==32); /* Symbol uses disp32 in both passes. */
        if (i==4)assert(e.size==5 && e.bytes[1]==0xf0 && e.bytes[4]==0xff);
        sys_pos+=e.size;
    }
    assert(sys_pos==total);
    char missing[]="movl $missing,%eax";ENCODING e;
    assert(encode_line(missing,2,&e)!=0);
    char duplicate[]="start: nop";assert(count_line(duplicate)!=0);
    char backward[]=".org 1";assert(count_line(backward)!=0);
    char small_symbol[]="movb $target,%al";
    sym_table[find_symbol_name("target")].abs_off=256;
    assert(encode_line(small_symbol,2,&e)!=0);
    printf("PASS: SMCE AT&T operands, MOV encodings, two passes and errors\n");
    return 0;
}
