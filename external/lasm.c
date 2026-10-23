/*
LWOS ASSEMBLER V1
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
DWORD sys_offset; // org
DWORD sys_pos; // pos
CHAR inst[20];
/* Split locally instead of the library's single-character strtok. Both passes
 * must recognize exactly the same instruction/operand boundaries. */
static PSTR next_word(PSTR *cursor) {
    PSTR p=*cursor;
    while (*p==' ' || *p=='\t')p++;
    if (!*p || *p==';') {*cursor=p;return 0;}
    PSTR word=p;
    while (*p && *p!=' ' && *p!='\t' && *p!=';')p++;
    if (*p==';') {*p=0;*cursor=p;}
    else {
        if (*p)*p++=0;
        *cursor=p;
    }
    return word;
}
static int split_line(PSTR line,PSTR *operand) {
    PSTR cursor=line,word=next_word(&cursor);
    inst[0]=0;*operand=0;
    if (!word)return 0;
    unsigned i=0;
    for (;word[i];i++) {
        if (i>=sizeof(inst)-1)return 1;
        inst[i]=to_upper((BYTE)word[i]);
    }
    inst[i]=0;
    *operand=next_word(&cursor);
    return 0;
}
static int count_line(PCHAR line) {
    PSTR operand;
    DWORD tmp;
    if (split_line(line,&operand))return 1;
    if (!*inst)return 0;
    if (!strcmp(inst,"ORG")) {
        if (!operand || !num_parse(operand,&tmp))return 1;
        sys_offset=tmp;
        return 0;
    }
    if (!strcmp(inst,"LABEL")) {
        if (!operand || strlen(operand)>=sizeof(sym_table[0].name))return 1;
        if (find_symbol_name(operand)!=-1)return 1;
        int slot=free_symbol();
        if (slot<0)return 1;
        strcpy(sym_table[slot].name,operand);
        sym_table[slot].abs_off=sys_pos+sys_offset;
        return 0;
    }
    if (!strcmp(inst,"NOP")) {sys_pos+=1;return 0;}
    if (!strcmp(inst,"DB") || !strcmp(inst,"DW") || !strcmp(inst,"DD")) {
        if (!operand || !num_parse(operand,&tmp))return 1;
        sys_pos+=!strcmp(inst,"DB") ? 1 : (!strcmp(inst,"DW") ? 2 : 4);
        return 0;
    }
    if (!strcmp(inst,"JMPS")) {
        if (!operand)return 1;
        sys_pos+=2;
        return 0;
    }
    return 1;
}
static int assemble_line(PCHAR line, PBYTE mach, DWORD cap, PDWORD size) {
    PSTR operand;
    DWORD tmp;
    *size=0;
    if (split_line(line,&operand))return 1;
    memzero(mach,cap);
    if (!*inst || !strcmp(inst,"ORG") || !strcmp(inst,"LABEL"))return 0;
    if (!strcmp(inst,"NOP")) {
        if (cap<1)return FS_ERR_RANGE;
        *size=1;mach[0]=0x90;
        return 0;
    }
    if (!strcmp(inst,"DB") || !strcmp(inst,"DW") || !strcmp(inst,"DD")) {
        if (!operand || !num_parse(operand,&tmp))return 1;
        DWORD width=!strcmp(inst,"DB") ? 1 : (!strcmp(inst,"DW") ? 2 : 4);
        if (cap<width)return FS_ERR_RANGE;
        for (DWORD i=0;i<width;i++)mach[i]=(BYTE)(tmp>>(i*8));
        *size=width;
        return 0;
    }
    if (!strcmp(inst,"JMPS")) {
        if (!operand)return 1;
        if (!num_parse(operand,&tmp)) {
            int slot=find_symbol_name(operand);
            if (slot<0)return 2;
            tmp=sym_table[slot].abs_off;
        }
        if (cap<2)return FS_ERR_RANGE;
        *size=2;mach[0]=0xeb;mach[1]=(BYTE)(tmp&0xff);
        return 0;
    }
    return 1;
}
CHAR linebuf[1024];
BYTE mach_code[32];
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
int error=0;
int main(int argc, char** argv) {
    PCSTR source,destination;
    int rc=parse_args(argc,argv,&source,&destination);
    if (rc) {
        printf("Usage: lasm FILE -o OUTPUT\n");
        return rc;
    }
    char path[256],output_path[256];
    rc=lw_path_resolve(lw_env_cwd(lw_env),source,path,sizeof(path));
    if (rc)return rc;
    rc=lw_path_resolve(lw_env_cwd(lw_env),destination,output_path,sizeof(output_path));
    if (rc)return rc;
    if (same_path(path,output_path)) {
        printf("lasm: input and output must be different files\n");
        return RM_ERR_ARGUMENT;
    }
    printf("path=\"%s\"\n",path);
    rc=fs_mount(&volume,lw_blk_get(0));
    if (rc)return rc;
    rc=fs_file_open(&volume,path,&file);
    if (rc)return rc;
    rc=fs_touch(&volume,output_path);
    if (rc)goto done;
    rc=fs_file_open(&volume,output_path,&output);
    if (rc)goto done;
    rc=fs_file_truncate(&output);
    if (rc)goto done;

    (void)free_symbol();

    memzero(sym_table,sizeof(sym_table));
    sys_offset=0,sys_pos=0;
    linenum=0;
    fpos=0;
    while ((rc=read_line(linebuf,sizeof(linebuf)))>0) {
        linenum++;
        rc=count_line(linebuf);
        if (rc) {
            printf("syntax error at line %u\n",linenum);
            error=1;
            break;
        }
    }
    rc=fs_file_seek(&file,0,FS_SEEK_SET);
    if (rc)goto done;
    if (error)goto done;
    linenum=0;
    fpos=0;
    fcount=0;
    while ((rc=read_line(linebuf,sizeof(linebuf)))>0) {
        linenum++;
        printf("%u:%s\n",linenum,linebuf);
        DWORD code_size=0;
        rc=assemble_line(
            linebuf,
            mach_code,
            sizeof(mach_code),
            &code_size
        );
        if (rc) {
            printf("syntax error at line %u\n",linenum);
            break;
        }
        DWORD written=0;
        rc=fs_file_write(
            &output, mach_code, code_size, &written
        );
        if (rc||written!=code_size) {
            if (!rc)rc=FS_ERR_IO;
            break;
        }
        rc=0;
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
