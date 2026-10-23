/*
SMCE — Symbolic Machine Code Editor
用于机器码的符号化、易读表达，不作为大型项目构建使用的编译器。
*/
#include "stdio.h"
#include "abi.h"
#include "resman.h"
#include "fs.h"
#include "crt.h"
#include "path.h"
#include "string.h"
#include "mem.h"
#include "convert.h"

#define MAX_SYMBOL 128

FS_VOLUME volume={0};
FS_FILE file={0};
FS_FILE output={0};
BYTE buffer[512];
DWORD fpos,fcount;

typedef struct _SYMBOL {
    CHAR name[32]; // 符号名
    DWORD abs_off; // 绝对偏移
    DWORD pnt_off; // 上级偏移
    DWORD stage; // 完成度 0无定义 1欠定义 2完全定义
} SYMBOL, *PSYMBOL;

SYMBOL sym_table[MAX_SYMBOL];

static int find_symbol_name(PCSTR name) {
    for (int i=0; i<MAX_SYMBOL; i++) {
        if (!strcmp(name,sym_table[i].name)) {
            return i;
        }
    }
    return -1;
}/*
static int find_symbol_aoff(DWORD off) {
    for (int i=0; i<MAX_SYMBOL; i++) {
        if (off==sym_table[i].abs_off)return i;
    }
    return -1;
}*/
static int free_symbol(void) {
    for (int i=0; i<MAX_SYMBOL; i++) {
        if (sym_table[i].name[0]==0)return i;
    }
    return -1; // out of range
}

static int read_line(PCHAR line, DWORD cap) {
    DWORD length=0;

    if (cap<2)return FS_ERR_ARGUMENT;
    for (;;) {
        if (fpos==fcount) {
            int rc=fs_file_read(
                &file,
                buffer,
                sizeof(buffer),
                &fcount
            );
            if (rc)return rc;
            fpos=0;
            if (!fcount) {
                if (!length)return 0;
                break;
            }
        }
        char ch=buffer[fpos++];
        if (ch=='\n')break;
        if (length>=cap-1)return FS_ERR_RANGE;
        line[length++]=ch;
    }
    if (length&&line[length-1]=='\r')length--;
    line[length]=(CHAR)0;
    return 1;
}
/* AT&T parser/encoder added with AI assistance. Both passes use encode_line;
 * pass 1 records symbols and sizes, pass 2 resolves values and emits bytes.
 * New instructions belong in encode_instruction, never in separate pass logic. */
DWORD sys_pos;
typedef struct { BYTE bytes[32]; DWORD size; int fill; } ENCODING;
typedef struct { int kind, width, reg, base, index, scale, symbolic; DWORD value; } OPERAND;
enum { OP_IMMEDIATE=1, OP_REGISTER, OP_MEMORY };

static PSTR trim(PSTR p) {
    while (*p==' ' || *p=='\t' || *p=='\r')p++;
    unsigned n=strlen(p);
    while (n && (p[n-1]==' ' || p[n-1]=='\t' || p[n-1]=='\r'))p[--n]=0;
    return p;
}
static int identifier(PCSTR p) {
    if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || *p=='_' || *p=='.'))return 0;
    for (p++;*p;p++)
        if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
              (*p>='0' && *p<='9') || *p=='_' || *p=='.'))return 0;
    return 1;
}
static int number(PCSTR p,PDWORD value) {
    int negative=0;
    if (*p=='+' || *p=='-')negative=*p++=='-';
    unsigned base=10;
    if (*p=='0') {
        base=8;p++;
        if (*p=='x' || *p=='X') {base=16;p++;if (!*p)return 0;}
        else if (!*p) {*value=0;return 1;}
    }
    if (!*p)return 0;
    DWORD v=0;
    for (;*p;p++) {
        unsigned d=(*p>='0' && *p<='9') ? (unsigned)(*p-'0') :
            (*p>='a' && *p<='f') ? (unsigned)(*p-'a'+10) :
            (*p>='A' && *p<='F') ? (unsigned)(*p-'A'+10) : 99;
        if (d>=base || v>(0xffffffffu-d)/base)return 0;
        v=v*base+d;
    }
    if (negative && v>0x80000000u)return 0;
    *value=negative ? 0u-v : v;
    return 1;
}
static int expression(PSTR text,int pass,PDWORD value,int *symbolic) {
    text=trim(text);*symbolic=0;
    if (number(text,value))return 0;
    if (!identifier(text))return 1;
    *symbolic=1;
    int slot=find_symbol_name(text);
    if (slot<0) {
        if (pass==2)return 1;
        *value=0;return 0;
    }
    *value=sym_table[slot].abs_off;
    return 0;
}
static int fits(DWORD v,int width) {
    if (width==1)return v<=0xff || v>=0xffffff80u;
    if (width==2)return v<=0xffff || v>=0xffff8000u;
    return 1;
}
static void emit(ENCODING *e,DWORD value,int width) {
    for (int i=0;i<width;i++)e->bytes[e->size++]=(BYTE)(value>>(i*8));
}
/* Split operands only at top-level commas, preserving disp(base,index,scale). */
static int operands(PSTR p,PSTR *args,int *count) {
    int depth=0;*count=0;
    if (!*trim(p))return 0;
    PSTR start=p;
    for (;;) {
        char c=*p;
        if (c=='(') {if (depth++)return 1;}
        if (c==')') {if (--depth<0)return 1;}
        if ((!c || c==',') && !depth) {
            if (*count>=8)return 1;
            *p=0;args[*count]=trim(start);
            if (!*args[(*count)++])return 1;
            if (!c)break;
            start=p+1;
        }
        if (!c)return 1;
        p++;
    }
    return depth!=0;
}
static int reg_parse(PSTR text,int *width,int *reg) {
    static const char *names[3][8]={
        {"AL","CL","DL","BL","AH","CH","DH","BH"},
        {"AX","CX","DX","BX","SP","BP","SI","DI"},
        {"EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI"}
    };
    if (*text++!='%')return 1;
    char name[4];unsigned n=0;
    while (*text && n<3)name[n++]=to_upper((BYTE)*text++);
    if (*text)return 1;
    name[n]=0;
    for (int w=0;w<3;w++)for (int r=0;r<8;r++) {
        if (!strcmp(name,names[w][r])) {*width=1<<w;*reg=r;return 0;}
    }
    return 1;
}
static int parse_operand(PSTR p,int pass,OPERAND *out) {
    memzero(out,sizeof(*out));out->base=out->index=-1;
    if (*p=='$') {
        out->kind=OP_IMMEDIATE;
        return expression(p+1,pass,&out->value,&out->symbolic);
    }
    if (*p=='%') {
        out->kind=OP_REGISTER;
        return reg_parse(p,&out->width,&out->reg);
    }
    out->kind=OP_MEMORY;
    PSTR open=strchr(p,'(');
    if (!open)return expression(p,pass,&out->value,&out->symbolic);
    *open++=0;
    if (*trim(p) && expression(p,pass,&out->value,&out->symbolic))return 1;
    PSTR close=strchr(open,')');
    if (!close || *trim(close+1))return 1;
    *close=0;
    PSTR parts[3]={open,0,0};int n=1;
    for (PSTR q=open;*q;q++)if (*q==',') {
        if (n>=3)return 1;
        *q=0;parts[n++]=q+1;
    }
    int width;
    if (*trim(parts[0]) && (reg_parse(trim(parts[0]),&width,&out->base) || width!=4))return 1;
    if (n>1 && *trim(parts[1]) &&
        (reg_parse(trim(parts[1]),&width,&out->index) || width!=4 || out->index==4))return 1;
    if (out->base<0 && out->index<0)return 1;
    if (n>1 && out->index<0)return 1;
    if (n>2) {
        DWORD scale;
        if (!number(trim(parts[2]),&scale) ||
            (scale!=1 && scale!=2 && scale!=4 && scale!=8))return 1;
        if (scale==2)out->scale=1;
        if (scale==4)out->scale=2;
        if (scale==8)out->scale=3;
    }
    return 0;
}
static void modrm(ENCODING *e,int reg,const OPERAND *operand) {
    if (operand->kind==OP_REGISTER) {emit(e,0xc0|(reg<<3)|operand->reg,1);return;}
    int base=operand->base,index=operand->index;
    int sib=index>=0 || base==4;
    int disp=base<0 || operand->symbolic ? 4 :
        !operand->value && base!=5 ? 0 :
        (operand->value<=127 || operand->value>=0xffffff80u) ? 1 : 4;
    int mod=base<0 ? 0 : disp==4 ? 2 : disp==1 ? 1 : 0;
    emit(e,(mod<<6)|(reg<<3)|(sib ? 4 : base<0 ? 5 : base),1);
    if (sib)emit(e,(operand->scale<<6)|((index<0 ? 4 : index)<<3)|(base<0 ? 5 : base),1);
    emit(e,operand->value,disp);
}
static int encode_mov(PCSTR name,PSTR *args,int count,int pass,ENCODING *e) {
    int width=0;
    if (!strcmp(name,"MOVB"))width=1;
    else if (!strcmp(name,"MOVW"))width=2;
    else if (!strcmp(name,"MOVL"))width=4;
    else if (strcmp(name,"MOV"))return 1;
    OPERAND src,dst;
    if (count!=2 || parse_operand(args[0],pass,&src) || parse_operand(args[1],pass,&dst))return 1;
    if (!width)width=src.kind==OP_REGISTER ? src.width : dst.kind==OP_REGISTER ? dst.width : 0;
    if (!width || dst.kind==OP_IMMEDIATE ||
        (src.kind==OP_MEMORY && dst.kind==OP_MEMORY) ||
        (src.kind==OP_REGISTER && src.width!=width) ||
        (dst.kind==OP_REGISTER && dst.width!=width))return 1;
    if (width==2)emit(e,0x66,1);
    if (src.kind==OP_IMMEDIATE) {
        if ((pass==2 || !src.symbolic) && !fits(src.value,width))return 1;
        if (dst.kind==OP_REGISTER)emit(e,(width==1 ? 0xb0 : 0xb8)+dst.reg,1);
        else {emit(e,width==1 ? 0xc6 : 0xc7,1);modrm(e,0,&dst);}
        emit(e,src.value,width);return 0;
    }
    /* Absolute accumulator loads/stores use the compact moffs encoding. */
    const OPERAND *memory=src.kind==OP_MEMORY ? &src : dst.kind==OP_MEMORY ? &dst : 0;
    const OPERAND *reg=src.kind==OP_REGISTER ? &src : &dst;
    if (memory && memory->base<0 && memory->index<0 && reg->reg==0) {
        emit(e,(src.kind==OP_MEMORY ? 0xa0 : 0xa2)+(width!=1),1);
        emit(e,memory->value,4);return 0;
    }
    if (src.kind==OP_REGISTER) {
        emit(e,width==1 ? 0x88 : 0x89,1);modrm(e,src.reg,&dst);
    } else {
        emit(e,width==1 ? 0x8a : 0x8b,1);modrm(e,dst.reg,&src);
    }
    return 0;
}
/* Dispatch here when adding another instruction; pass sizing stays shared. */
static int encode_instruction(PCSTR name,PSTR *args,int count,int pass,ENCODING *e) {
    if (!strcmp(name,"NOP")) {
        if (count)return 1;
        emit(e,0x90,1);return 0;
    }
    /* Fixed near JMP avoids instruction-size changes on forward references. */
    if (!strcmp(name,"JMP")) {
        DWORD target;int symbolic;
        if (count!=1 || expression(args[0],pass,&target,&symbolic))return 1;
        emit(e,0xe9,1);emit(e,target-(sys_pos+5),4);return 0;
    }
    return encode_mov(name,args,count,pass,e);
}
static int encode_line(PSTR line,int pass,ENCODING *e) {
    memzero(e,sizeof(*e));
    PSTR comment=strchr(line,'#');if (comment)*comment=0;
    comment=strchr(line,';');if (comment)*comment=0;
    PSTR p=trim(line),colon=strchr(p,':');
    if (colon) {
        *colon=0;PSTR label=trim(p);
        if (!identifier(label) || strlen(label)>=sizeof(sym_table[0].name))return 1;
        int slot=find_symbol_name(label);
        if (pass==1) {
            if (slot>=0 || (slot=free_symbol())<0)return 1;
            strcpy(sym_table[slot].name,label);sym_table[slot].abs_off=sys_pos;
        } else if (slot<0 || sym_table[slot].abs_off!=sys_pos)return 1;
        p=trim(colon+1);
    }
    if (!*p)return 0;
    char name[20];unsigned n=0;
    while (*p && *p!=' ' && *p!='\t') {
        if (n>=sizeof(name)-1)return 1;
        name[n++]=to_upper((BYTE)*p++);
    }
    name[n]=0;
    PSTR args[8];int count;
    if (operands(trim(p),args,&count))return 1;
    if (!strcmp(name,".ORG")) {
        DWORD target;
        if (count!=1 || *args[0]=='-' || !number(args[0],&target) || target<sys_pos)return 1;
        e->fill=1;e->size=target-sys_pos;
    } else if (!strcmp(name,".BYTE") || !strcmp(name,".WORD") || !strcmp(name,".LONG")) {
        int width=!strcmp(name,".BYTE") ? 1 : !strcmp(name,".WORD") ? 2 : 4;
        if (!count)return 1;
        for (int i=0;i<count;i++) {
            DWORD value;int symbolic;
            if (expression(args[i],pass,&value,&symbolic) ||
                ((pass==2 || !symbolic) && !fits(value,width)))return 1;
            emit(e,value,width);
        }
    } else if (encode_instruction(name,args,count,pass,e))return 1;
    if (e->size>0xffffffffu-sys_pos)return FS_ERR_RANGE;
    return 0;
}
static int count_line(PCHAR line) {
    ENCODING e;int rc=encode_line(line,1,&e);
    if (!rc)sys_pos+=e.size;
    return rc;
}
CHAR linebuf[1024];
DWORD linenum=0;
static int parse_args(int argc,char **argv,PCSTR *source,PCSTR *destination) {
    int options=1;
    *source=0;*destination=0;
    for (int i=1;i<argc;i++) {
        PCSTR arg=argv[i];
        if (options && !strcmp(arg,"--")) {options=0;continue;}
        if (options && !strcmp(arg,"-o")) {
            if (*destination || i+1>=argc || !*argv[i+1] || argv[i+1][0]=='-')
                return RM_ERR_ARGUMENT;
            *destination=argv[++i];
        } else {
            if (!*arg || (options && *arg=='-') || *source)return RM_ERR_ARGUMENT;
            *source=arg;
        }
    }
    return *source && *destination ? 0 : RM_ERR_ARGUMENT;
}
/* FAT names are case insensitive; never truncate the source as output. */
static int same_path(PCSTR a,PCSTR b) {
    while (*a && to_upper((BYTE)*a)==to_upper((BYTE)*b)) {a++;b++;}
    return !*a && !*b;
}
int main(int argc, char** argv) {
    PCSTR source,destination;
    int rc=parse_args(argc,argv,&source,&destination);
    if (rc) {
        printf("Usage: smce FILE -o OUTPUT\n");
        return rc;
    }
    char path[256],output_path[256];
    rc=lw_path_resolve(lw_env_cwd(lw_env),source,path,sizeof(path));
    if (rc)return rc;
    rc=lw_path_resolve(lw_env_cwd(lw_env),destination,output_path,sizeof(output_path));
    if (rc)return rc;
    if (same_path(path,output_path)) {
        printf("smce: input and output must be different files\n");
        return RM_ERR_ARGUMENT;
    }
    printf("path=\"%s\"\n",path);
    rc=fs_mount(&volume,lw_blk_get(0));
    if (rc)return rc;
    rc=fs_file_open(&volume,path,&file);
    if (rc)return rc;
    memzero(sym_table,sizeof(sym_table));
    sys_pos=0;
    linenum=0;
    fpos=fcount=0;
    while ((rc=read_line(linebuf,sizeof(linebuf)))>0) {
        linenum++;
        rc=count_line(linebuf);
        if (rc) {
            printf("syntax error at line %u\n",linenum);
            goto done;
        }
    }
    if (rc<0)goto done;
    rc=fs_file_seek(&file,0,FS_SEEK_SET);
    if (rc)goto done;
    rc=fs_touch(&volume,output_path);
    if (rc)goto done;
    rc=fs_file_open(&volume,output_path,&output);
    if (rc)goto done;
    rc=fs_file_truncate(&output);
    if (rc)goto done;

    linenum=0;
    sys_pos=0;
    fpos=fcount=0;
    while ((rc=read_line(linebuf,sizeof(linebuf)))>0) {
        linenum++;
        ENCODING e;
        rc=encode_line(linebuf,2,&e);
        if (rc) {
            printf("syntax error at line %u\n",linenum);
            break;
        }
        if (e.fill)memzero(e.bytes,sizeof(e.bytes));
        DWORD remaining=e.size;
        while (remaining) {
            DWORD written=0,n=remaining>sizeof(e.bytes) ? sizeof(e.bytes) : remaining;
            rc=fs_file_write(&output,e.bytes,n,&written);
            if (rc || written!=n) {
                if (!rc)rc=FS_ERR_IO;
                goto done;
            }
            remaining-=n;
        }
        sys_pos+=e.size;
    }
    printf("--------------------------------\nSYMBOL TABLE\n");
    printf("     ABS_OFF  PNT_OFF  NAME\n");
    for (int i=0; i<MAX_SYMBOL; i++) {
        if (sym_table[i].name[0]) {
            printf("%3u: %08X %08X %s\n",i,
                sym_table[i].abs_off,
                sym_table[i].pnt_off,
                sym_table[i].name
            );
        }
    }

done:
    fs_file_close(&output);
    fs_file_close(&file);
    return rc;
}
