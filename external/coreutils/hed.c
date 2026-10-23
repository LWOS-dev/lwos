/* Byte-oriented command editor. Written locally; shares ed's terminal adapter,
 * not its line-oriented text representation (NUL/newline are ordinary bytes).
 */
#include "port/compat.h"

#define HED_MAX (4u*1024u*1024u)
typedef struct {
    BYTE *data,*undo;
    DWORD length,cursor,undo_length,undo_cursor;
    int dirty,undo_dirty,have_undo,quiet;
    char name[256];
} HED;

static const char *spaces(const char *s) {while (*s==' ' || *s=='\t')s++;return s;}
static int hex(int c) {
    if (c>='0' && c<='9')return c-'0';
    if (c>='a' && c<='f')return c-'a'+10;
    if (c>='A' && c<='F')return c-'A'+10;
    return -1;
}
static void *allocate(DWORD n) {return rm_alloc(n ? n : 1,0,0);}
static void release(void *p) {if (p)rm_free(p);}
static void forget_undo(HED *e) {release(e->undo);e->undo=0;e->have_undo=0;}
static void cleanup(HED *e) {release(e->data);forget_undo(e);}
static int path(const char *s,char out[256]) {
    return !*s ? RM_ERR_ARGUMENT : lw_path_resolve(lw_env_cwd(lw_env),s,out,256);
}

/* Replace an interval atomically in memory, retaining one undo snapshot.
 * No edits or allocation failure change either the buffer or its undo state.
 */
static int splice(HED *e,DWORD at,DWORD count,const BYTE *bytes,DWORD n) {
    if (at>e->length || count>e->length-at || n>HED_MAX-(e->length-count))return -1;
    if (!count && !n)return 0;
    DWORD size=e->length-count+n;
    BYTE *next=allocate(size);
    if (!next)return -1;
    if (at)memcpy(next,e->data,at);
    if (n)memcpy(next+at,bytes,n);
    if (e->length>at+count)
        memcpy(next+at+n,e->data+at+count,e->length-at-count);
    forget_undo(e);
    e->undo=e->data;e->undo_length=e->length;e->undo_cursor=e->cursor;
    e->undo_dirty=e->dirty;e->have_undo=1;
    e->data=next;e->length=size;e->dirty=1;
    e->cursor=n ? at+n-1 : (at<size ? at : (size ? size-1 : 0));
    return 0;
}
static void undo(HED *e) {
    BYTE *p=e->data;e->data=e->undo;e->undo=p;
    DWORD n=e->length;e->length=e->undo_length;e->undo_length=n;
    n=e->cursor;e->cursor=e->undo_cursor;e->undo_cursor=n;
    int dirty=e->dirty;e->dirty=e->undo_dirty;e->undo_dirty=dirty;
}
static int load(HED *e,const char *name) {
    FS_VOLUME volume={0};FS_FILE file={0};char resolved[256];
    int rc=path(name,resolved);
    if (!rc)rc=fs_mount(&volume,lw_blk_get(0));
    if (!rc)rc=fs_file_open(&volume,resolved,&file);
    if (rc==FS_ERR_NOT_FOUND) {
        strcpy(e->name,resolved);
        if (!e->quiet)printf("%s [new file]\n",resolved);
        return 0;
    }
    if (rc)return rc;
    DWORD size=0,done=0;
    rc=fs_file_size(&file,&size);
    BYTE *data=0;
    if (!rc && size>HED_MAX)rc=FS_ERR_RANGE;
    if (!rc && !(data=allocate(size)))rc=FS_ERR_NOFREE;
    while (!rc && done<size) {
        DWORD n=0;rc=fs_file_read(&file,data+done,size-done,&n);
        if (!rc && (!n || n>size-done))rc=FS_ERR_IO;
        if (!rc)done+=n;
    }
    int close_rc=fs_file_close(&file);if (!rc)rc=close_rc;
    if (rc) {release(data);return rc;}
    e->data=data;e->length=size;strcpy(e->name,resolved);
    if (!e->quiet)printf("%u bytes\n",size);
    return 0;
}
static int save(HED *e,const char *name) {
    char resolved[256];FS_VOLUME volume={0};FS_FILE file={0};
    int rc=path(*name ? name : e->name,resolved);
    if (!rc)rc=fs_mount(&volume,lw_blk_get(0));
    if (!rc)rc=fs_file_open(&volume,resolved,&file);
    if (rc==FS_ERR_NOT_FOUND) {
        rc=fs_touch(&volume,resolved);
        if (!rc)rc=fs_file_open(&volume,resolved,&file);
    }
    if (rc)return rc;
    /* Use the existing FAT API: w is an explicit overwrite, not an atomic
     * filesystem replacement. On any failure keep the in-memory edits dirty.
     */
    e->dirty=1;
    rc=fs_file_truncate(&file);
    DWORD done=0;
    while (!rc && done<e->length) {
        DWORD n=0;rc=fs_file_write(&file,e->data+done,e->length-done,&n);
        if (!rc && (!n || n>e->length-done))rc=FS_ERR_IO;
        if (!rc)done+=n;
    }
    int close_rc=fs_file_close(&file);if (!rc)rc=close_rc;
    if (rc)return rc;
    strcpy(e->name,resolved);e->dirty=0;
    /* The saved file now represents this snapshot, not the old undo buffer. */
    if (e->have_undo)e->undo_dirty=1;
    if (!e->quiet)printf("%u bytes written\n",done);
    return 0;
}
static int address(const char **text,HED *e,DWORD *out) {
    const char *s=spaces(*text);DWORD value=0;
    if (*s=='.') {value=e->cursor;s++;}
    else if (*s=='$') {value=e->length ? e->length-1 : 0;s++;}
    else if (*s>='0' && *s<='9') {
        unsigned base=10;
        if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) {base=16;s+=2;}
        const char *first=s;
        while (hex(*s)>=0 && (unsigned)hex(*s)<base) {
            unsigned digit=hex(*s++);
            if (value>(0xffffffffu-digit)/base)return -1;
            value=value*base+digit;
        }
        if (s==first)return -1;
    } else return 0;
    *out=value;*text=spaces(s);return 1;
}
static int decode(const char *s,BYTE *out,DWORD *length) {
    int high=-1;DWORD n=0;
    for (;*s;s++) {
        if (*s==' ' || *s=='\t')continue;
        int v=hex(*s);if (v<0)return -1;
        if (high<0)high=v;
        else {if (n>=512)return -1;out[n++]=(high<<4)|v;high=-1;}
    }
    if (high>=0)return -1;
    *length=n;return 0;
}
static void print_bytes(HED *e,DWORD first,DWORD last) {
    for (DWORD at=first;at<=last;) {
        DWORD n=last-at+1;if (n>16)n=16;
        printf("%08x: ",at);
        for (DWORD i=0;i<16;i++) {
            if (i<n)printf("%02x ",e->data[at+i]);else printf("   ");
        }
        printf(" |");
        for (DWORD i=0;i<n;i++) {
            BYTE c=e->data[at+i];putchar(c>=32 && c<=126 ? c : '.');
        }
        printf("|\n");at+=n;
    }
    e->cursor=last;
}
static void help(void) {
    printf("Addresses: zero-based decimal or 0xHEX; . current, $ last, %% all.\n");
    printf("Use spaces before commands with hex addresses: 0x10 c ff\n");
    printf("[range] p       hex/ASCII dump (16 bytes without an end address)\n");
    printf("[addr] a HEX    append after byte; i HEX inserts before byte\n");
    printf("[range] c HEX   replace range, allowing a different byte count\n");
    printf("[addr] o HEX    overwrite bytes without changing file size\n");
    printf("[range] d       delete bytes; u toggles the last edit\n");
    printf("a/i/c without HEX: enter hex lines, then . on a line by itself\n");
    printf("= size; [addr] = offset; f [FILE] name; w [FILE] write\n");
    printf("q quit if saved; Q discard and quit; h help; empty line next dump\n");
    printf("Maximum buffer: 4 MiB. Byte input is hexadecimal, e.g. 00 ff 4142.\n");
}

/* 1: quit, 0: continue. Invalid commands never partially modify the buffer. */
static int command(HED *e,char *line) {
    const char *s=spaces(line);DWORD first=e->cursor,last=first;
    int addressed=0,range=0,parsed;
    if (*s=='%') {s=spaces(s+1);first=0;last=e->length ? e->length-1 : 0;addressed=range=1;}
    else {
        parsed=address(&s,e,&first);if (parsed<0)goto bad;
        addressed=parsed;last=first;
        if (*s==',') {
            if (!addressed)first=0;
            s++;parsed=address(&s,e,&last);
            if (parsed<0)goto bad;
            if (!parsed)last=e->length ? e->length-1 : 0;
            addressed=range=1;
        }
    }
    int op=*s;if (op)s=spaces(s+1);
    if (!op) {
        op='p';
        if (!addressed)first=e->cursor+(e->length ? 1 : 0);
    }
    if (op=='p') {
        if (*s)goto bad;
        if (!e->length && !addressed) {printf("(empty)\n");return 0;}
        if (first>=e->length)goto bad;
        if (!range)last=e->length-first>16 ? first+15 : e->length-1;
        if (last<first || last>=e->length)goto bad;
        print_bytes(e,first,last);return 0;
    }
    if (op=='=') {
        if (*s || range)goto bad;
        DWORD n=addressed ? first : e->length;
        printf("%u (0x%x)\n",n,n);return 0;
    }
    if (op=='d' || op=='a' || op=='i' || op=='c' || op=='o') {
        if (first>e->length || (range && (last<first || last>=e->length)))goto bad;
        DWORD at=first,count=0;
        if (op=='d' || op=='c') {
            if (first>=e->length)goto bad;
            count=last-first+1;
        }
        if (op=='d') {if (*s || splice(e,at,count,0,0))goto bad;return 0;}
        if ((op=='a' || op=='i' || op=='o') && range)goto bad;
        if (op=='a' && e->length) {if (first>=e->length)goto bad;at++;}
        BYTE bytes[512];DWORD n=0;
        BYTE *input=0;DWORD length=0;
        if (*s) {
            if (decode(s,bytes,&n) || !n)goto bad;
            input=bytes;length=n;
        } else {
            if (op=='o')goto bad;
            char text[1024];
            for (;;) {
                int got=port_readline(0,"",text,sizeof(text));
                if (got<=0) {release(input);printf("? input cancelled\n");return 0;}
                if (got && text[got-1]=='\n')text[--got]=0;
                const char *t=spaces(text);
                if (*t=='.' && !*spaces(t+1))break;
                if (decode(t,bytes,&n) || n>HED_MAX-length) {release(input);goto bad;}
                if (!n)continue;
                BYTE *next=allocate(length+n);
                if (!next) {release(input);goto bad;}
                if (length)memcpy(next,input,length);
                memcpy(next+length,bytes,n);release(input);input=next;length+=n;
            }
        }
        if (op=='o')count=length;
        int rc=splice(e,at,count,input,length);
        if (input!=bytes)release(input);
        if (rc)goto bad;
        return 0;
    }
    if (addressed)goto bad;
    if (op=='w') {
        int rc=save(e,s);if (rc)printf("? write error %d; buffer retained\n",rc);
        return 0;
    }
    if (op=='f') {
        if (*s) {
            char resolved[256];if (path(s,resolved))goto bad;
            strcpy(e->name,resolved);
        }
        printf("%s\n",*e->name ? e->name : "[no filename]");return 0;
    }
    if (*s)goto bad;
    if (op=='u') {if (!e->have_undo)goto bad;undo(e);return 0;}
    if (op=='h') {help();return 0;}
    if (op=='Q')return 1;
    if (op=='q') {
        if (e->dirty)printf("? modified buffer; use w to save or Q to discard\n");
        return !e->dirty;
    }
bad:
    printf("? invalid command, range, hex data, or insufficient memory\n");return 0;
}
static int hed_main(int argc,char **argv) {
    HED e={0};const char *name=0,*prompt="hed> ";int options=1;
    for (int i=1;i<argc;i++) {
        const char *s=argv[i];
        if (options && !strcmp(s,"--")) {options=0;continue;}
        if (options && !strcmp(s,"-s")) {e.quiet=1;continue;}
        if (options && !strcmp(s,"-h")) {help();return 0;}
        if (options && !strcmp(s,"-p") && i+1<argc) {prompt=argv[++i];continue;}
        if ((options && *s=='-') || name) {
            printf("Usage: hed [-s] [-p PROMPT] [--] [FILE]\n");return RM_ERR_ARGUMENT;
        }
        name=s;
    }
    if (name) {
        int rc=load(&e,name);
        if (rc) {printf("hed: %s: error %d\n",name,rc);cleanup(&e);return rc;}
    }
    char line[1024];int result=0;
    for (;;) {
        int n=port_readline(0,prompt,line,sizeof(line));
        if (n<0) {
            if (e.dirty) {printf("? EOF: changes not saved\n");result=1;}
            break;
        }
        if (!n)continue; /* Ctrl-C or rejected/overlong terminal line. */
        if (n && line[n-1]=='\n')line[--n]=0;
        while (n && (line[n-1]==' ' || line[n-1]=='\t'))line[--n]=0;
        if (command(&e,line))break;
    }
    cleanup(&e);return result;
}
int main(int argc,char **argv) {return port_run(hed_main,argc,argv);}
