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

FS_VOLUME volume={0};
FS_FILE file={0};
FS_FILE output={0};
BYTE buffer[512];
DWORD fpos,fcount;

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
CHAR inst[20];
static int assemble_line(PCHAR line, PBYTE mach, DWORD cap, PDWORD size) {
    PSTR p=line;
    PSTR q=inst;
    int rc;
    DWORD tmp;
    q=strtok(p,' ');
    memzero(inst,20);
    for (int i=0; p[i]; i++)inst[i]=to_upper(p[i]);
    memzero(mach, cap);
    //printf("\nINSTRUCTION %s\n",inst);
    if (!strcmp(inst,"NOP")) {
        *size=1;
        mach[0]=0x90;
        return 0;
    }
    if (!strcmp(inst,"DB")) {
        *size=1;
        p=q;
        q=strtok(p,' ');
        memzero(inst,20);
        strcpy(inst,p);
        rc=num_parse(inst,&tmp);
        if (!rc)return rc;
        mach[0]=(BYTE)(tmp&0xff);
        return 0;
    }
    if (!strcmp(inst,"DW")) {
        *size=2;
        p=q;
        q=strtok(p,' ');
        memzero(inst,20);
        strcpy(inst,p);
        rc=num_parse(inst,&tmp);
        if (!rc)return rc;
        mach[0]=(BYTE)(tmp&0xff);
        mach[1]=(BYTE)((tmp>>8)&0xff);
        return 0;
    }
    if (!strcmp(inst,"DD")) {
        *size=4;
        p=q;
        q=strtok(p,' ');
        memzero(inst,20);
        strcpy(inst,p);
        rc=num_parse(inst,&tmp);
        if (!rc)return rc;
        mach[0]=(BYTE)(tmp&0xff);
        mach[1]=(BYTE)((tmp>>8)&0xff);
        mach[2]=(BYTE)((tmp>>16)&0xff);
        mach[3]=(BYTE)((tmp>>24)&0xff);
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

done:
    fs_file_close(&output);
    fs_file_close(&file);
    return rc;
}
