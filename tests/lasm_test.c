/* Both assembler passes must agree regardless of whitespace layout. */
#include <assert.h>
#define main lasm_main
#include "../external/lasm.c"
#undef main

static void check(PCSTR source,DWORD width,DWORD value) {
    char first[1024],second[1024];BYTE code[32];DWORD size=99;
    strcpy(first,source);strcpy(second,source);
    sys_pos=0;
    assert(count_line(first)==0 && sys_pos==width);
    assert(assemble_line(second,code,sizeof(code),&size)==0 && size==width);
    for (DWORD i=0;i<width;i++)assert(code[i]==(BYTE)(value>>(i*8)));
}
int main(void) {
    check("db 12",1,12);
    check("db    12",1,12);
    check("   db   12   ",1,12);
    check("\tDb\t \t0FFh\t",1,255);
    check("  dw    1234h ; word",2,0x1234);
    check("\tDD\t12345678h;dword",4,0x12345678);
    check("NOP",1,0x90);
    check("  nop\t ; comment",1,0x90);
    check("",0,0);check(" \t ",0,0);
    check("; comment",0,0);check(" \t; indented comment",0,0);
    char origin[]=" \torg   100h ";
    sys_offset=0;assert(count_line(origin)==0 && sys_offset==0x100);
    char label[]=" \tlabel   target\t; symbol";
    sys_pos=7;assert(count_line(label)==0);
    int slot=find_symbol_name("target");assert(slot>=0 && sym_table[slot].abs_off==0x107);
    char jump[]=" \tjmps   target ; reference";BYTE code[32];DWORD size;
    assert(assemble_line(jump,code,sizeof(code),&size)==0 && size==2 && code[0]==0xeb);
    const char *bad[]={"db", "dw    ", "dd xyz", "org", "label", "jmps", "averylonginstructionname 1"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        char text[1024];strcpy(text,bad[i]);assert(count_line(text)!=0);
    }
    char invalid[]=" db   invalid";
    assert(assemble_line(invalid,code,sizeof(code),&size)!=0 && !size);
    return 0;
}
