#include "stdio.h"
#include "abi.h"
#include "fs.h"
#include "crt.h"
#include "path.h"

typedef struct {
    DWORD columns,group,start,length;
    int plain,upper,limited;
} XXD_OPTIONS;

/* Decimal or 0x-prefixed byte counts; reject overflow and trailing junk. */
static int number(PCSTR p,PDWORD value) {
    DWORD result=0,base=10;
    if (p[0]=='0' && (p[1]=='x' || p[1]=='X')) {base=16;p+=2;}
    if (!*p)return RM_ERR_ARGUMENT;
    while (*p) {
        unsigned digit;
        int c=*p++;
        if (c>='0' && c<='9')digit=c-'0';
        else if (c>='a' && c<='f')digit=c-'a'+10;
        else if (c>='A' && c<='F')digit=c-'A'+10;
        else return RM_ERR_ARGUMENT;
        if (digit>=base || result>(0xffffffffu-digit)/base)return RM_ERR_ARGUMENT;
        result=result*base+digit;
    }
    *value=result;return 0;
}

static int print_row(PCBYTE bytes,DWORD count,DWORD offset,const XXD_OPTIONS *options) {
    const char *hex=options->upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char row[272];unsigned n=0;
    if (!options->plain) {
        for (int shift=28;shift>=0;shift-=4)row[n++]=hex[(offset>>shift)&15];
        row[n++]=':';row[n++]=' ';
    }
    DWORD slots=options->plain ? count : options->columns;
    for (DWORD i=0;i<slots;i++) {
        if (i<count) {
            row[n++]=hex[bytes[i]>>4];row[n++]=hex[bytes[i]&15];
        } else {row[n++]=' ';row[n++]=' ';}
        if (!options->plain && options->group && (i+1)%options->group==0)
            row[n++]=' ';
    }
    if (!options->plain) {
        /* Complete the last group separator when the columns are uneven. */
        if (!options->group || options->columns%options->group)row[n++]=' ';
        row[n++]=' ';
        for (DWORD i=0;i<count;i++)row[n++]=bytes[i]>=32 && bytes[i]<=126 ? bytes[i] : '.';
    }
    row[n++]='\n';row[n]=0;
    return printf("%s",row)<0 ? EOF : 0;
}

static int dump_file(PCSTR path,const XXD_OPTIONS *options) {
    FS_VOLUME volume={0};FS_FILE file={0};
    BYTE row[64];DWORD filled=0,offset=options->start,remaining=options->length;
    int rc=fs_mount(&volume,lw_blk_get(0));
    if (rc)return rc;
    rc=fs_file_open(&volume,path,&file);
    if (rc)return rc;
    if (options->start) {
        DWORD size;
        rc=fs_file_size(&file,&size);
        if (rc || options->start>=size)goto done;
        rc=fs_file_seek(&file,options->start,FS_SEEK_SET);
        if (rc)goto done;
    }
    for (;;) {
        DWORD wanted=options->columns-filled,count=0;
        if (options->limited && wanted>remaining)wanted=remaining;
        if (!wanted)break;
        rc=fs_file_read(&file,row+filled,wanted,&count);
        filled+=count;
        if (options->limited)remaining-=count;
        if (filled==options->columns || rc || !count || (options->limited && !remaining)) {
            if (filled) {
                int output_rc=print_row(row,filled,offset,options);
                if (output_rc) {rc=output_rc;break;}
                offset+=filled;filled=0;
            }
        }
        if (rc || !count)break;
    }
done:
    fs_file_close(&file);
    return rc;
}

int main(int argc,char **argv) {
    XXD_OPTIONS options={.columns=16,.group=2};
    PCSTR filename=0;int columns_set=0,end_options=0;
    for (int i=1;i<argc;i++) {
        PCSTR arg=argv[i];
        if (!arg || !*arg)goto usage;
        if (!end_options && arg[0]=='-') {
            if (arg[1]=='-' && !arg[2]) {end_options=1;continue;}
            int option=arg[1];
            if ((option=='p' || option=='u' || option=='h') && !arg[2]) {
                if (option=='h')goto help;
                if (option=='p')options.plain=1;
                else options.upper=1;
                continue;
            }
            if (option!='c' && option!='g' && option!='s' && option!='l')goto usage;
            PCSTR operand=arg+2;DWORD value;
            if (!*operand) {
                if (++i>=argc || !argv[i])goto usage;
                operand=argv[i];
            }
            if (number(operand,&value))goto usage;
            switch (option) {
                case 'c':if (!value || value>64)goto usage;
                         options.columns=value;columns_set=1;break;
                case 'g':if (value>64)goto usage;options.group=value;break;
                case 's':options.start=value;break;
                case 'l':options.length=value;options.limited=1;break;
            }
        } else {
            if (filename)goto usage;
            filename=arg;
        }
    }
    if (!filename)goto usage;
    if (options.plain && !columns_set)options.columns=30;
    char path[256];
    int rc=lw_path_resolve(lw_env_cwd(lw_env),filename,path,sizeof(path));
    if (!rc)rc=dump_file(path,&options);
    if (rc)printf("xxd: %s: error %d\n",filename,rc);
    return rc;
help:
    printf("Usage: xxd [-p] [-u] [-c COLS] [-g BYTES] [-s OFFSET] [-l LEN] [--] FILE\n");
    printf("COLS: 1..64; -g 0 disables grouping. Counts: decimal or 0x hex.\n");
    return 0;
usage:
    printf("Usage: xxd [-p] [-u] [-c COLS] [-g BYTES] [-s OFFSET] [-l LEN] [--] FILE\n");
    return RM_ERR_ARGUMENT;
}
