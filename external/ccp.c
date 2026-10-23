#include "stdio.h"
#include "console.h"
#include "crt.h"
#include "convert.h"
#include "string.h"
#include "abi.h"
#include "fs.h"
#include "path.h"
#include "mem.h"

CHAR prompt[256];
CHAR cwd[256]="/BIN";
static CHAR search_path[256]="/BIN";
PCHAR pmt_p;
/* 返回字符或 VK_*；失败返回 EOF。不回显。 */
static int console_readkey(void) {
    LW_WINDOW_EVENT event;
    DWORD id = lw_env->vi.window_id;
    for (;;) {
        int rc=rm_window_poll(id, &event);
        if (rc<0)return EOF;
        if (rc==1) {
            if (event.type==LW_WINDOW_KEY)return event.value;
            continue; /* 跳过焦点通知 */
        }
        if (rm_window_wait(id)!=RM_OK)
            return EOF;
    }
}
static void readline(void) {
    PSTR p=prompt;
    for (;;) {
        DWORD t;
        t=console_readkey();
        switch (t) {
            case EOF:return;
            case VK_ENTER:goto done;
            case VK_BKSPACE:{if (p>prompt) {printf("\b \b");*--p=0;}break;}
            default:{if (t>=32 && t<127 && p<prompt+sizeof(prompt)-1) {*(p++)=(CHAR)t;putchar(t);}break;}
        }
    }
done:
    *p=0;
    printf("\n");
}
static int program_path(PCSTR command,PSTR path) {
    return lw_path_resolve(cwd,command,path,256);
}
static int executable_path(PCSTR base,PCSTR command,PSTR path) {
    int rc=lw_path_resolve(base,command,path,256);
    if (rc)return rc;
    PCSTR name=path;
    for (PCSTR p=path;*p;p++)if (*p=='/')name=p+1;
    if (!strchr(name,'.')) {
        if (strlen(path)>251)return RM_ERR_ARGUMENT;
        strcat(path,".EXE");
    }
    return 0;
}
static int open_program(PFS_VOLUME volume,PCSTR command,PSTR path,PFS_FILE file) {
    int rc=executable_path(cwd,command,path);
    if (rc)return rc;
    rc=fs_file_open(volume,path,file);
    if (strchr(command,'/') || rc!=FS_ERR_NOT_FOUND)return rc;
    PCSTR p=search_path;
    while (*p) {
        char base[256];int n=0;
        while (*p && *p!=':')base[n++]=*p++;
        base[n]=0;
        if (*p)p++;
        rc=executable_path(base,command,path);
        if (rc)return rc;
        rc=fs_file_open(volume,path,file);
        if (rc!=FS_ERR_NOT_FOUND)return rc;
    }
    return rc;
}
/* One whitespace-delimited operand, shared by cd and path. */
static int operand(PSTR value) {
    PCSTR p=pmt_p ? pmt_p : "";
    skip_ws(&p);int n=0;
    while (*p && *p!=' ' && *p!='\t') {
        if (n>=255)return RM_ERR_ARGUMENT;
        value[n++]=*p++;
    }
    value[n]=0;skip_ws(&p);
    return *p ? RM_ERR_ARGUMENT : 0;
}
static int change_directory(void) {
    char arg[256],resolved[256];FS_VOLUME volume={0};SFT_T entry;
    int rc=operand(arg);
    if (rc)return rc;
    rc=program_path(*arg ? arg : "/BIN",resolved);
    if (rc)return rc;
    rc=fs_mount(&volume,lw_blk_get(0));
    if (rc)return rc;
    rc=fs_lookup(&volume,resolved,&entry);
    if (rc)return rc;
    if (!(entry.attr&0x10))return FS_ERR_ARGUMENT;
    strcpy(cwd,resolved);return 0;
}
static int set_path(void) {
    char arg[256],result[256];int used=0;
    int rc=operand(arg);
    if (rc)return rc;
    if (!*arg) {printf("%s\n",search_path);return 0;}
    if (!strcmp(arg,"-")) {search_path[0]=0;return 0;}
    PSTR p=arg;
    while (*p) {
        char resolved[256];PSTR start=p;
        while (*p && *p!=':')p++;
        int more=*p;
        if (more)*p++=0;
        if (!*start || (more && !*p))return RM_ERR_ARGUMENT;
        rc=program_path(start,resolved);
        if (rc)return rc;
        int length=strlen(resolved);
        if (used+!!used+length>255)return RM_ERR_ARGUMENT;
        if (used)result[used++]=':';
        strcpy(result+used,resolved);used+=length;
    }
    strcpy(search_path,result);return 0;
}
static int run_program(PSTR command) {
    char path[256];
    /* Arguments are compacted in place, with shell-style quotes/escaping. */
    char *argv[32];
    int argc=1, rc=0, exit_code=0;
    FS_VOLUME volume={0};
    FS_FILE file={0};
    PRM_IMAGE image=0;
    PVOID bytes=0;
    DWORD size=0, count=0;
    argv[0]=command;
    PSTR p=pmt_p;
    while (p && *p) {
        while (*p==' ' || *p=='\t')p++;
        if (!*p)break;
        if (argc>=(int)(sizeof(argv)/sizeof(argv[0]))-1)return RM_ERR_ARGUMENT;
        PSTR out=p;
        argv[argc++]=out;
        int quote=0;
        while (*p) {
            int c=*p++;
            if (!quote && (c==' ' || c=='\t'))break;
            if (c=='\'' || c=='"') {
                if (!quote) {quote=c;continue;}
                if (quote==c) {quote=0;continue;}
            }
            if (c=='\\' && quote!='\'') {
                if (!*p)return RM_ERR_ARGUMENT;
                /* Double quotes only unescape quote/backslash; preserve regex. */
                if (!quote || *p=='"' || *p=='\\')c=*p++;
            }
            *out++=c;
        }
        if (quote)return RM_ERR_ARGUMENT;
        *out=0;
    }
    argv[argc]=0;
    rc=fs_mount(&volume,lw_blk_get(0));
    if (rc)goto done;
    rc=open_program(&volume,command,path,&file);
    if (rc)goto done;
    rc=fs_file_size(&file,&size);
    if (rc)goto done;
    if (!size || size>0x100000u) {rc=RM_ERR_FORMAT;goto done;}
    bytes=rm_alloc(size,0,0);
    if (!bytes) {rc=RM_ERR_MEMORY;goto done;}
    rc=fs_file_read(&file,bytes,size,&count);
    if (rc)goto done;
    if (count!=size) {rc=FS_ERR_IO;goto done;}
    rc=rm_image_load(bytes,size,RM_OWNER_SYSTEM,&image);
    if (rc)goto done;
    rm_free(bytes);bytes=0;
    fs_file_close(&file);
    LW_ENV env={0};
    memcpy(&env,lw_env,lw_env->size<sizeof(env) ? lw_env->size : sizeof(env));
    env.size=sizeof(env);
    strcpy(env.cwd,cwd);
    env.argc=argc;env.argv=argv;
    rc=rm_image_run(image,&env,&exit_code);
done:
    fs_file_close(&file);
    if (bytes)rm_free(bytes);
    if (image) {
        int unload_rc=rm_image_unload(image);
        if (!rc)rc=unload_rc;
    }
    return rc ? rc : exit_code;
}
DWORD errno=0;
DWORD execute(PSTR p) {
    if (!p)return -1;
    if (!strcmp(p,"pwd")) { printf("%s\n",cwd); return 0; }
    if (!strcmp(p,"echo")) { printf("%s\n",pmt_p); return 0; }
    if (!strcmp(p,"cd"))return change_directory();
    if (!strcmp(p,"path"))return set_path();
    return *p ? run_program(p) : 0;
}
int main(void) {
    strcpy(cwd,"/BIN");
    strcpy(search_path,"/BIN");
    stdio_set_attr(0x0f);
    printf("LWOS CCP v1.0 - Meow Technology\n");
    for (;;) {
        printf("[%s]$ ",cwd);
        readline();
        PCSTR command=prompt;
        skip_ws(&command);
        pmt_p=(PSTR)command;
        while (*pmt_p && *pmt_p!=' ' && *pmt_p!='\t')pmt_p++;
        if (*pmt_p)*pmt_p++=0;
        errno=execute((PSTR)command);
        if (errno)printf("%d\n",errno);
    }
    return 0;
}
