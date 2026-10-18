#include "../include/tui.h"
#include "../include/keys.h"

#include "abi.h"
#include "fs.h"
#include "string.h"
#include "convert.h"
#include "mem.h"
#include "lwp.h"
#include "resman.h"

PWORD video;
PWORD subvideo;
BYTE vi_attr;
DWORD focus;

void putxy(const char* str, int x, int y) {
    const char* p=str;
    int length=strlen(p);
    for (int i=0; i<length; i++) {
        *(video+x+y*80+i)=(((WORD)vi_attr)<<8)|(*(p+i));
    }
}
void putxyn(const char* str, int x, int y, int n) {
    const char* p=str;
    for (int i=0; i<n; i++) {
        *(video+x+y*80+i)=(((WORD)vi_attr)<<8)|(*(p+i));
    }
}
void putxyc(BYTE c, int x, int y) {
    *(video+x+y*80)=(((WORD)vi_attr)<<8)|(c);
}
static void set_attr(BYTE fore, BYTE back) {
    vi_attr=fore|(back<<4);
}

TUI_AREA area_mem_reader;
TUI_AREA area_mem_dump;
TUI_AREA area_file_info;
TUI_AREA area_file_expl;
TUI_AREA area_pci_list;
TUI_AREA area_pci_info;
TUI_AREA area_mem_pool;

static int menu_items=7;
static int menu_index=0;
static int tuim_page=0;

const char name_menu[][7]={
    "MENU",
    "RESET",
    "HALT",
    "RCS",
    "PCIDEV",
    "IRQS",
    "MEM",
    "TASKS"
};
const char name_units[4][6]={" BYTE"," WORD","DWORD","QWORD"};
const char name_area_mr[]="MEM READER";
const char name_area_md[]="MEM DUMP";
const char name_area_fi[]="FILE INFO";
const char name_area_fe[]="FILE EXPLORER";
const char name_area_pl[]="PCI LIST";
const char name_area_pi[]="PCI INFO";
const char name_area_mp[]="MEMORY POOL";

void draw_area(PTUI_AREA p) {
    set_attr((focus==p->index)?(TUI_YELLOW|TUI_LIGHT):(TUI_WHITE),TUI_BLUE);
    if (p->index==-1u)set_attr(TUI_WHITE,TUI_BLUE);
    for (DWORD i=0; i<p->w; i++) {
        putxyc(196,p->x+i,p->y);
        putxyc(196,p->x+i,p->y+p->h);
    }
    for (DWORD i=0; i<p->h; i++) {
        putxyc(179,p->x,p->y+i);
        putxyc(179,p->x+p->w,p->y+i);
    }
    putxyc(218,p->x,p->y);
    putxyc(191,p->x+p->w,p->y);
    putxyc(192,p->x,p->y+p->h);
    putxyc(217,p->x+p->w,p->y+p->h);
    if (focus==p->index) {
        set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE|TUI_LIGHT);
    } else {
        set_attr(TUI_WHITE,TUI_BLUE);
    }
    if (p->index==-1u)set_attr(TUI_AQUA,TUI_BLUE);
    putxy(p->name,p->x+1,p->y);
}
void draw_menu() {
    if (focus==0) {
        set_attr(TUI_BLACK,TUI_AQUA|TUI_LIGHT);
    } else {
        set_attr(TUI_WHITE,TUI_BLUE);
    }
    putxyn(name_menu[0],0,1,6);
    for (int i=0; i<menu_items; i++) {
        if (menu_index==i) {
            set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
        } else {
            set_attr(TUI_WHITE,TUI_BLUE);
        }
        putxyn(name_menu[i+1],0,i+2,6);
    }
}
void proc_menu(DWORD ctrl) {
    if (focus!=0)return;
    switch (ctrl) {
        case VK_UP: {
            menu_index--;
            if (menu_index<0)menu_index=0;
            break;
        }
        case VK_DOWN: {
            menu_index++;
            if (menu_index>=menu_items)menu_index=menu_items-1;
            break;
        }
        case 0xa: {
            if (menu_index<2)break;
            tuim_page=menu_index-2;
            for (int i=0; i<80*25; i++)video[i]=0x1020;
            break;
        }
        default: {
            break;
        }
    }
}
static int type_ptr=0;
static CHAR type_buf[17];
extern FS_VOLUME monitor_fs;
static DWORD fe_current_cluster;
/* Bounded BSS storage until resman exposes allocation services. The linker
 * checks the complete monitor image/stack against the next reserved region. */
#define FE_CAPACITY 256u
#define HEX_CAPACITY (64u * 1024u)
static SFT_T filelist_ptr[FE_CAPACITY];
static DWORD filelist_length, fe_index;
static char fe_path[256] = "/";
static int fe_error, fe_truncated;
static PCSTR fe_message;
static BYTE hex_buffer[HEX_CAPACITY], hex_staging[HEX_CAPACITY];
static FS_FILE hex_handles[2];
static PFS_FILE hex_file=&hex_handles[0];
static char hex_path[256];
static DWORD hex_length, hex_cursor;
static int hex_active, hex_dirty, hex_pending, hex_digits;
static int hex_confirm, hex_failed, hex_error;
static DWORD hex_written;
static PCSTR hex_message;

static void clear_rows(int first, int last) {
    for (int y=first; y<=last; y++)
        for (int x=0; x<80; x++)putxyc(' ',x,y);
}

static void proc_fe_update_list(void) {
    DWORD cur=fe_current_cluster;
    filelist_length=0;
    fe_index=0;
    fe_error=0;
    fe_truncated=0;
    fe_message=0;
    if (!monitor_fs.mounted) {
        fe_error=FS_ERR_NOT_MOUNTED;
        return;
    }
    for (DWORD hops=0; hops<monitor_fs.cluster_count; hops++) {
        int entries=monitor_fs.sectors_per_cluster*16;
        for (int i=0; i<entries; i++) {
            SFT_T entry;
            int status=fat_read_fte(&monitor_fs,cur,i,&entry);
            if (status==FS_ENTRY_END)return;
            if (status<0) { fe_error=status; goto failed; }
            if (status!=FS_ENTRY_VALID)continue;
            if (filelist_length==FE_CAPACITY) {
                fe_truncated=1;
                return;
            }
            filelist_ptr[filelist_length++]=entry;
        }
        int status=next_cluster(&monitor_fs,cur,&cur);
        if (!status)return;
        if (status<0) { fe_error=status; goto failed; }
    }
    fe_error=FS_ERR_FORMAT;
failed:
    filelist_length=0;
}

/* Build a root-relative path; fs_file_open does not accept dot components. */
static int fe_entry_path(const SFT_T *entry, PSTR out) {
    strcpy(out,fe_path);
    if (!strncmp(entry->name,".          ",11))return 0;
    if (!strncmp(entry->name,"..         ",11)) {
        int n=strlen(out);
        while (n>1 && out[n-1]!='/')n--;
        if (n>1)n--;
        out[n]=0;
        return 0;
    }
    int n=strlen(out), base=8, ext=3;
    while (base && entry->name[base-1]==' ')base--;
    while (ext && entry->name[8+ext-1]==' ')ext--;
    if (!base || n+1+base+(ext ? ext+1 : 0)>=256)return FS_ERR_RANGE;
    if (n>1)out[n++]='/';
    memcpy(out+n,entry->name,base); n+=base;
    if (ext) { out[n++]='.'; memcpy(out+n,entry->name+8,ext); n+=ext; }
    out[n]=0;
    return 0;
}

/* Load via a fresh handle and staging buffer: failed reloads leave edits intact. */
static int hex_load(PCSTR path) {
    PFS_FILE next=hex_file==&hex_handles[0] ? &hex_handles[1] : &hex_handles[0];
    DWORD count=0;
    int err=fs_file_open(&monitor_fs,path,next);
    if (!err && next->length>HEX_CAPACITY)err=FS_ERR_RANGE;
    if (!err)err=fs_file_read(next,hex_staging,next->length,&count);
    if (!err && count!=next->length)err=FS_ERR_IO;
    if (err) { fs_file_close(next); return err; }
    fs_file_close(hex_file);
    hex_file=next;
    hex_length=next->length;
    memcpy(hex_buffer,hex_staging,hex_length);
    hex_cursor=0;
    hex_dirty=hex_digits=hex_pending=hex_failed=hex_error=hex_confirm=0;
    hex_written=0;
    hex_message="LOADED";
    return 0;
}

static void hex_open(const SFT_T *entry) {
    if (entry->attr & 0x10) { fe_message="SELECT A FILE"; return; }
    char path[256];
    int err=fe_entry_path(entry,path);
    if (!err)err=hex_load(path);
    if (err) { fe_error=err; fe_message="OPEN FAILED (MAX 64 KiB)"; return; }
    strcpy(hex_path,path);
    hex_active=1;
}

static void hex_close(void) {
    fs_file_close(hex_file);
    hex_active=hex_dirty=hex_digits=hex_confirm=0;
    proc_fe_update_list();
    set_attr(TUI_WHITE,TUI_BLUE);
    clear_rows(0,24);
}

static void hex_save(void) {
    if (hex_digits) { hex_message="ENTER commits pending byte; ESC cancels it"; return; }
    if (hex_failed) { hex_message="SAVE BLOCKED: reload or exit after I/O failure"; return; }
    if (hex_file->attr & 1) { hex_message="READ ONLY"; return; }
    hex_written=0;
    int err=fs_file_seek(hex_file,0,FS_SEEK_SET);
    if (!err)err=fs_file_write(hex_file,hex_buffer,hex_length,&hex_written);
    if (!err && hex_written!=hex_length)err=FS_ERR_IO;
    if (!err)err=fs_sync(&monitor_fs);
    hex_error=err;
    if (err) {
        /* A partial write/flush failure may already have changed the disk.
         * Keep edited bytes, but require a fresh load before another save. */
        hex_failed=1;
        hex_message="SAVE FAILED; disk may be partly changed. F1 reload / ESC exit";
    } else {
        hex_dirty=0;
        hex_message="SAVED";
    }
}

static void hex_draw(void) {
    char text[9];
    DWORD offset=(hex_cursor/256)*256;
    set_attr(TUI_WHITE,TUI_BLUE);
    clear_rows(0,24);
    putxy("LWOS HEX EDITOR",1,0);
    putxyn(hex_path,18,0,strlen(hex_path)>60 ? 60 : strlen(hex_path));
    putxy("OFFSET",1,1); hex_xlat(offset,text,8); putxy(text,8,1);
    putxy("LENGTH",20,1); hex_xlat(hex_length,text,8); putxy(text,27,1);
    putxy(hex_dirty ? "MODIFIED" : "CLEAN",38,1);
    if (hex_file->attr & 1)putxy("READ ONLY",50,1);
    for (int j=0;j<16;j++) { hex_xlat(j,text,2); putxy(text,11+j*3,2); }
    for (int i=0;i<16;i++) {
        DWORD row=offset+i*16;
        set_attr(TUI_WHITE,TUI_BLUE);
        hex_xlat(row,text,8); putxy(text,1,3+i);
        for (int j=0;j<16;j++) {
            DWORD pos=row+j;
            set_attr(pos==hex_cursor ? TUI_BLACK : TUI_WHITE,
                     pos==hex_cursor ? TUI_WHITE : TUI_BLUE);
            if (pos<hex_length)hex_xlat(hex_buffer[pos],text,2);
            else { text[0]='-'; text[1]='-'; text[2]=0; }
            if (pos==hex_cursor && hex_digits) {
                hex_xlat(hex_pending,text,2);
                if (hex_digits==1)text[0]='_';
            }
            putxy(text,11+j*3,3+i);
            set_attr(TUI_WHITE,TUI_BLUE);
            putxyc(pos<hex_length ? to_avail(hex_buffer[pos]) : ' ',61+j,3+i);
        }
    }
    set_attr(TUI_WHITE,TUI_BLUE);
    putxy(hex_message ? hex_message : "",1,20);
    putxy("ERR=",1,21); hex_xlat((DWORD)hex_error,text,8); putxy(text,5,21);
    putxy("WRITTEN=",17,21); hex_xlat(hex_written,text,8); putxy(text,25,21);
    putxy("ARROWS / PGUP / PGDN / HOME / END: move   HEX HEX + ENTER: commit",1,23);
    putxy("F1=RELOAD  F2=SAVE  ESC/F3=EXIT  Limit=64 KiB",1,24);
    if (hex_confirm)putxy("Discard edits? Y=yes  N/ESC=no",1,22);
}
static void proc_hex(DWORD key) {
    if (hex_confirm) {
        if (key=='y' || key=='Y') {
            int action=hex_confirm;
            hex_confirm=0;
            if (action==2) { hex_close(); return; }
            hex_error=hex_load(hex_path);
            if (hex_error)hex_message="RELOAD FAILED; edits retained";
        } else if (key=='n' || key=='N' || key==VK_ESC)hex_confirm=0;
        hex_draw(); return;
    }
    if (key==VK_ESC && hex_digits) { hex_digits=hex_pending=0; }
    else if (key==VK_ESC || key==VK_F3) {
        if (hex_dirty || hex_digits)hex_confirm=2;
        else { hex_close(); return; }
    } else if (key==VK_F1) {
        if (hex_dirty || hex_digits)hex_confirm=1;
        else {
            hex_error=hex_load(hex_path);
            if (hex_error)hex_message="RELOAD FAILED; buffer retained";
        }
    } else if (key==VK_F2)hex_save();
    else if (key==VK_ENTER && hex_digits==2) {
        if (!(hex_file->attr & 1) && hex_cursor<HEX_CAPACITY) {
            if (hex_cursor>=hex_length) {
                memzero(hex_buffer+hex_length,hex_cursor+1-hex_length);
                hex_length=hex_cursor+1;
                hex_dirty=1;
            }
            if (hex_buffer[hex_cursor]!=(BYTE)hex_pending)hex_dirty=1;
            hex_buffer[hex_cursor]=(BYTE)hex_pending;
        }
        hex_digits=hex_pending=0;
    } else if (key<0x100 && hex_digit((char)key)!=0xff) {
        if (hex_file->attr & 1)hex_message="READ ONLY";
        else if (hex_digits<2) {
            hex_pending=(hex_pending<<4)|hex_digit((char)key);
            hex_digits++;
        }
    } else {
        DWORD old=hex_cursor;
        switch (key) {
            case VK_LEFT: if (hex_cursor)hex_cursor--; break;
            case VK_RIGHT: if (hex_cursor<HEX_CAPACITY-1)hex_cursor++; break;
            case VK_UP: if (hex_cursor>=16)hex_cursor-=16; break;
            case VK_DOWN: if (hex_cursor<HEX_CAPACITY-16)hex_cursor+=16; break;
            case VK_PGUP: hex_cursor=hex_cursor>=256 ? hex_cursor-256 : 0; break;
            case VK_PGDN: hex_cursor=hex_cursor<HEX_CAPACITY-256 ? hex_cursor+256 : HEX_CAPACITY-1; break;
            case VK_HOME: hex_cursor=0; break;
            case VK_END: hex_cursor=hex_length<HEX_CAPACITY ? hex_length : HEX_CAPACITY-1; break;
        }
        if (old!=hex_cursor)hex_digits=hex_pending=0;
    }
    hex_draw();
}
static void subproc_fi(PTUI_AREA a, DWORD fileindex) {
    char text[9];
    set_attr(TUI_WHITE,TUI_BLUE);
    for (DWORD y=1;y<a->h;y++)putxy("         ",a->x+1,a->y+y);
    if (fileindex>=filelist_length)return;
    const SFT_T *entry=&filelist_ptr[fileindex];
    putxy("LENGTH",a->x+1,a->y+1);
    hex_xlat(entry->length,text,8); putxy(text,a->x+1,a->y+2);
    putxy("CLUSTER",a->x+1,a->y+3);
    hex_xlat(entry->cluster,text,8); putxy(text,a->x+1,a->y+4);
    putxy("ATTR",a->x+1,a->y+5);
    bin_xlat(entry->attr,text,8); putxy(text,a->x+1,a->y+6);
    putxy("CURRENT",a->x+1,a->y+7);
    hex_xlat(fe_current_cluster,text,8); putxy(text,a->x+1,a->y+8);
}
static void showmessage(const char* ctx) {
    memcpy(subvideo,video,80*25*sizeof(WORD));
    int window_width=strlen(ctx)+4;
    set_attr(TUI_BLACK,TUI_WHITE);
    for (int i=0; i<window_width; i++) {
        for (int j=0; j<3; j++) {
            putxyc(' ',40-window_width/2+i,11+j);
        }
    }
    putxy(ctx,40-window_width/2+2,12);
    lw_getk();
    memcpy(video,subvideo,80*25*sizeof(WORD));
}
PBYTE exe_file_buf;
DWORD exe_length;
DWORD exe_read_cnt;
FS_FILE exe_file;
PRM_IMAGE exe_image=0;
static void proc_fe(PTUI_AREA a, DWORD ctrl) {
    char text[100];
    char path[256];
    set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
    if (focus==a->index) {
        putxy("F5=REFRESH  F2/ENTER=ENTER DIR  F6=HEX EDITOR  UP/DOWN=SELECT",0,23);
        putxy("F4=EXECUTE",0,24);
        switch (ctrl) {
            case VK_F4: {
                DWORD exit_code=0;
                if (fe_index>=filelist_length)break;
                if (filelist_ptr[fe_index].attr&0x10)break;
                memzero(path,256);
                if (fe_entry_path(&filelist_ptr[fe_index],path))break;
                showmessage(path);
                exe_read_cnt=0;
                exe_length=0;
                memzero(&exe_file,sizeof(FS_FILE));
                int err;
                err=fs_file_open(&monitor_fs,path,&exe_file);
                if (err) {
                    memcpy(text,"ERR FILE OPEN",13);
                    hex_xlat(err,text+13,8);
                    showmessage(text);
                    break;
                }
                err=fs_file_size(&exe_file,&exe_length);
                if (err) {
                    memcpy(text,"ERR FILE SIZE",13);
                    hex_xlat(err,text+13,8);
                    showmessage(text);
                    break;
                }
                if (exe_length<sizeof(LwpHdr)) {
                    showmessage("INVALID EXE FILE");
                    break;
                }
                exe_file_buf=kmalloc(exe_length);
                if (!exe_file_buf || 
                    exe_file_buf==(PVOID)-1 || 
                    exe_file_buf==(PVOID)-2) {
                    exe_file_buf=0;
                    showmessage("MEMORY FAULT");
                    break;
                }
                memcpy(text,"ALLOCATED AT ",14);
                hex_xlat((DWORD)exe_file_buf,text+13,8);
                showmessage(text);
                err=fs_file_read(&exe_file,exe_file_buf,exe_length,&exe_read_cnt);
                if (!err&&exe_read_cnt!=exe_length) {
                    showmessage("INFO MISMATCH");
                }
                fs_file_close(&exe_file);
                err=rm_image_load(exe_file_buf,exe_length,RM_OWNER_SYSTEM,&exe_image);
                if (err) {
                    memcpy(text,"ERR LOAD",9);
                    hex_xlat(err,text+8,8);
                    showmessage(text);
                    kfree(exe_file_buf);
                    break;
                }
                err=rm_image_run(exe_image,0,&exit_code);
                if (err) {
                    memcpy(text,"ERR RUN ",9);
                    hex_xlat(err,text+8,8);
                    showmessage(text);
                    kfree(exe_file_buf);
                    break;
                }
                memcpy(text,"EXIT WITH ",10);
                hex_xlat(exit_code,text+10,8);
                showmessage(text);
                err=rm_image_unload(exe_image);
                if (err) {
                    memcpy(text,"ERR UNLOAD ",11);
                    hex_xlat(err,text+11,8);
                    showmessage(text);
                    kfree(exe_file_buf);
                    break;
                }
                break;
            }
            case VK_F5: proc_fe_update_list(); break;
            case VK_UP: if (fe_index)fe_index--; break;
            case VK_DOWN: if (fe_index+1<filelist_length)fe_index++; break;
            case VK_F6:
                if (fe_index<filelist_length)hex_open(&filelist_ptr[fe_index]);
                break;
            case VK_ENTER:
            case VK_F2:
                if (fe_index<filelist_length && (filelist_ptr[fe_index].attr & 0x10)) {
                    char path[256];
                    SFT_T target;
                    int err=fe_entry_path(&filelist_ptr[fe_index],path);
                    if (!err)err=fs_lookup(&monitor_fs,path,&target);
                    if (err) { fe_error=err; break; }
                    strcpy(fe_path,path);
                    fe_current_cluster=target.cluster;
                    proc_fe_update_list();
                }
                break;
        }
    }
    DWORD top=(fe_index/18)*18;
    for (DWORD row=0;row<18;row++) {
        DWORD i=top+row;
        set_attr(TUI_WHITE,TUI_BLUE);
        putxy("                          ",a->x+1,a->y+1+row);
        if (i>=filelist_length)continue;
        if (i==fe_index)set_attr(focus==a->index ? TUI_BLACK : TUI_WHITE|TUI_LIGHT,
                                focus==a->index ? TUI_WHITE : TUI_BLUE);
        putxyn(filelist_ptr[i].name,a->x+1,a->y+1+row,8);
        if (filelist_ptr[i].attr & 0x10)putxy("<DIR>",a->x+9,a->y+1+row);
        else putxyn(filelist_ptr[i].name+8,a->x+10,a->y+1+row,3);
        hex_xlat(filelist_ptr[i].cluster,text,8);
        putxy(text,a->x+16,a->y+1+row);
    }
    set_attr(TUI_WHITE,TUI_BLUE);
    putxy("                          ",a->x+1,a->y+19);
    putxy("                          ",a->x+1,a->y+20);
    if (fe_error) {
        putxy("ERR=",a->x+1,a->y+19);
        hex_xlat((DWORD)fe_error,text,8); putxy(text,a->x+5,a->y+19);
    } else if (fe_truncated)putxy("FIRST 256 ENTRIES ONLY",a->x+1,a->y+19);
    else if (!filelist_length)putxy("EMPTY DIRECTORY",a->x+1,a->y+19);
    if (fe_message)putxy(fe_message,a->x+1,a->y+20);
    subproc_fi(&area_file_info,fe_index);
}
static void proc_md(PTUI_AREA a, DWORD ctrl) {
    static int index=0; // 放两排dump
    static DWORD offset[2]={0x400000,0x400000};
    static int unit[2];
    CHAR tstrdw[9];
    CHAR tstrqw[17];
    static int typing=0;
    if (focus!=a->index)goto draw;
    set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
    putxy("F4=SUBMIT           SPACE=ENTER OFFSET  F6=UNIT DEC",0,23);
    putxy("PGUP/PGDN=OFFSET    \x18\x19=INDEX            F7=UNIT INC",0,24);
    switch (ctrl) {
        case VK_UP: {
            index=0;
            break;
        }
        case VK_DOWN: {
            index=1;
            break;
        }
        case VK_F6: {
            unit[index]--;
            if (unit[index]<0)unit[index]=0;
            break;
        }
        case VK_F7: {
            unit[index]++;
            if (unit[index]>3)unit[index]=3;
            break;
        }
        case VK_PGUP: {
            offset[index]-=32;
            break;
        }
        case VK_PGDN: {
            offset[index]+=32;
            break;
        }
        case ' ': {
            if (typing==0) {
                typing=1;
                memzero(type_buf, 17);
                type_ptr=0;
                break;
            }
            break;
        }
        case VK_F4: {
            PCSTR input=type_buf;
            hex_parse(&input,&offset[index]);
            typing=0;
            break;
        }
    }
    if (typing==1 && ctrl<0x100 && hex_digit((char)ctrl)!=0xff && type_ptr<8) {
        type_buf[type_ptr++]=(char)ctrl;
        type_buf[type_ptr]=0;
        if (type_ptr==8) {
            PCSTR input=type_buf;
            hex_parse(&input,&offset[index]);
            typing=0;
        }
    }
    draw:
    for (int i=0; i<2; i++) {
        for (int j=0; j<4; j++) {
            DWORD data[2];
            PVOID pdata=(PVOID)data;
            for (int k=0; k<2; k++) {
                data[k]=*((PDWORD)offset[i]+k+j*2);
            }
            if (i==index) {
                set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
            } else {
                set_attr(TUI_WHITE,TUI_BLUE);
            }
            switch (unit[i]) {
                case 0: {
                    putxy("+0 +1 +2 +3 +4 +5 +6 +7",a->x+10,a->y+i*5+1);
                    break;
                }
                case 1: {
                    putxy("  +0    +2    +4    +6 ",a->x+10,a->y+i*5+1);
                    break;
                }
                case 2: {
                    putxy("      +0          +4   ",a->x+10,a->y+i*5+1);
                    break;
                }
                case 3: {
                    putxy("              +0       ",a->x+10,a->y+i*5+1);
                    break;
                }
            }
            hex_xlat(offset[i],tstrdw,8);
            putxyn(tstrdw,a->x+1,a->y+1+i*5,8);
            hex_xlat(offset[i]+j*8,tstrdw,8);
            putxyn(tstrdw,a->x+1,a->y+j+2+i*5,8);
            switch (unit[i]) {
                case 0: {
                    for (int k=0; k<8; k++) {
                        hex_xlat(*((PBYTE)pdata+k),tstrqw,2);
                        putxy("   ",a->x+k*3+10,a->y+i*5+2+j);
                        putxyn(tstrqw,a->x+k*3+10,a->y+i*5+2+j,2);
                    }
                    break;
                }
                case 1: {
                    for (int k=0; k<4; k++) {
                        hex_xlat(*((PWORD)pdata+k),tstrqw,4);
                        putxy("      ",a->x+k*6+10,a->y+i*5+2+j);
                        putxyn(tstrqw,a->x+k*6+10,a->y+i*5+2+j,4);
                    }
                    break;
                }
                case 2: {
                    for (int k=0; k<2; k++) {
                        hex_xlat(*((PDWORD)pdata+k),tstrqw,8);
                        putxy("            ",a->x+k*12+10,a->y+i*5+2+j);
                        putxyn(tstrqw,a->x+k*12+10,a->y+i*5+2+j,8);
                    }
                    break;
                }
                case 3: {
                    hex_xlat(*((PQWORD)pdata),tstrqw,16);
                    putxy("                        ",a->x+10,a->y+i*5+2+j);
                    putxyn(tstrqw,a->x+10,a->y+i*5+2+j,16);
                    break;
                }
            }
            for (int k=0; k<8; k++) {
                BYTE b=*((PBYTE)data+k);
                putxyc(to_avail(b),a->x+34+k,a->y+i*5+2+j);
            }
        }
        putxyn(name_units[unit[i]],a->x+34,a->y+1+i*5,5);
    }
    if (typing==1) {
        set_attr(TUI_BLACK,TUI_WHITE);
        putxy("        ",a->x+1,a->y+1+index*5);
        putxyn(type_buf,a->x+1,a->y+1+index*5,type_ptr);
    }
}
static void proc_mr(PTUI_AREA a, DWORD ctrl) {
    static int index=0;
    static int s=0;
    static int stt_a=0; // status addr
    static int mat=0; // modify addr ptr
    static DWORD addr[8] = {
        0x400000,0x400000,0x400000,0x400000,
        0x400000,0x400000,0x400000,0x400000
    };
    static QWORD tmp=0;
    static int type[8]={2,2,2,2,2,2,2,2}; // 保留每行的类型：0db 1dw 2dd 3dq

    CHAR tstrdw[9];
    CHAR tstrqw[17];

    if (focus!=a->index)goto draw;

    set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
    putxy("F4=SUBMIT       SPACE=ENTER     \x18\x19=INDEX",0,23);
    putxy("F6=UNIT DEC     F7=UNIT INC     \x1a\x1b=COLUMN",0,24);
    switch (ctrl) {
        case VK_UP: {
            index--;
            if (index<0)index=0;
            break;
        }
        case VK_DOWN: {
            index++;
            if (index>7)index=7;
            break;
        }
        case VK_RIGHT: {
            s++;
            if (s>2)s=2;
            break;
        }
        case VK_LEFT: {
            s--;
            if (s<0)s=0;
            break;
        }
        case VK_F6: {
            if (s==0) {
                type[index]--;
                if (type[index]<0)type[index]=0;
            }
            break;
        }
        case VK_F7: {
            if (s==0) {
                type[index]++;
                if (type[index]>3)type[index]=3;
            }
            break;
        }
        case ' ': {
            if (s==1 && stt_a==0) {
                stt_a=1;
                mat=index;
                type_ptr=0;
                memzero(type_buf,17);
                break;
            }
            if (s==2 && stt_a==0) {
                stt_a=2;
                mat=index;
                type_ptr=0;
                memzero(type_buf,17);
                break;
            }
            break;
        }
        case VK_F4: {
            if (s==1 && stt_a==1) {
                PCSTR input=type_buf;
                hex_parse(&input,&addr[mat]);
                stt_a=0;
                break;
            }
            if (s==2 && stt_a==2) {
                PCSTR input=type_buf;
                hex_parse_w(&input,&tmp);
                switch (type[index]) {
                    case 0: {
                        *(PBYTE)((PVOID)addr[index])=(BYTE)(tmp&0xff);
                        break;
                    }
                    case 1: {
                        *(PWORD)((PVOID)addr[index])=(WORD)(tmp&0xffff);
                        break;
                    }
                    case 2: {
                        *(PDWORD)((PVOID)addr[index])=(DWORD)(tmp&0xffffffff);
                        break;
                    }
                    case 3: {
                        *(PQWORD)((PVOID)addr[index])=(QWORD)(tmp);
                        break;
                    }
                }
                stt_a=0;
                break;
            }
            break;
        }
    }
    if (stt_a==1 && ctrl<0x100 && hex_digit((char)ctrl)!=0xff && type_ptr<8) {
        type_buf[type_ptr++]=(char)ctrl;
        type_buf[type_ptr]=0;
        if (type_ptr==8) {
            PCSTR input=type_buf;
            hex_parse(&input,&addr[mat]);
            stt_a=0;
        }
    }
    if (stt_a==2 && ctrl<0x100 && hex_digit((char)ctrl)!=0xff && type_ptr<16) {
        type_buf[type_ptr++]=(char)ctrl;
        type_buf[type_ptr]=0;
        if (type_ptr==16) {
            PCSTR input=type_buf;
            hex_parse_w(&input,&tmp);
            stt_a=0;
            switch (type[index]) {
                case 0: {
                    *(PBYTE)((PVOID)addr[index])=(BYTE)(tmp&0xff);
                    break;
                }
                case 1: {
                    *(PWORD)((PVOID)addr[index])=(WORD)(tmp&0xffff);
                    break;
                }
                case 2: {
                    *(PDWORD)((PVOID)addr[index])=(DWORD)(tmp&0xffffffff);
                    break;
                }
                case 3: {
                    *(PQWORD)((PVOID)addr[index])=(QWORD)(tmp);
                    break;
                }
            }
        }
    }
    draw:
    for (int i=0;i<8;i++) {
        if (i==index && s==0) {
            set_attr(TUI_BLACK,TUI_WHITE);
        } else {
            set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
        }
        putxy(name_units[type[i]],a->x+1,a->y+1+i);
        if (i==index && s==1) {
            set_attr(TUI_BLACK,TUI_WHITE);
        } else {
            set_attr(TUI_WHITE,TUI_BLUE);
        }
        hex_xlat(addr[i],tstrdw,8);
        putxy(tstrdw,a->x+7,a->y+1+i);
        set_attr(TUI_BLACK|TUI_LIGHT,TUI_BLUE);
        putxy("0000000000000000",a->x+16,a->y+1+i);
        if (i==index && s==2) {
            set_attr(TUI_BLACK,TUI_WHITE);
        } else {
            set_attr(TUI_WHITE,TUI_BLUE);
        }
        switch (type[i]) {
            case 0: {
                hex_xlat(*(PBYTE)((PVOID)(addr[i])),tstrqw,2);
                break;
            }
            case 1: {
                hex_xlat(*(PWORD)((PVOID)(addr[i])),tstrqw,4);
                break;
            }
            case 2: {
                hex_xlat(*(PDWORD)((PVOID)(addr[i])),tstrqw,8);
                break;
            }
            case 3: {
                hex_xlat(*(PQWORD)((PVOID)(addr[i])),tstrqw,16);
                break;
            }
        }
        putxy(tstrqw,a->x+16,a->y+1+i);
    }
    if (stt_a==1) {
        set_attr(TUI_BLACK,TUI_WHITE);
        putxy("        ",a->x+7,a->y+1+mat);
        putxyn(type_buf,a->x+7,a->y+1+mat,type_ptr);
    }
    if (stt_a==2) {
        set_attr(TUI_BLACK,TUI_WHITE);
        putxy("                ",a->x+16,a->y+1+mat);
        putxyn(type_buf,a->x+16,a->y+1+mat,type_ptr);
    }
}
enum {
    BAR_SIZE_UNKNOWN,
    BAR_SIZE_VALID,
    BAR_SIZE_FAILED
};
enum {
    BAR_IO,
    BAR_MEM32,
    BAR_MEM64
};
typedef struct _PCI_BAR_INFO {
    QWORD base,size;
    DWORD raw_lo,raw_hi;
    BYTE slot,slots,kind,prefetchable;
    BYTE size_state;
    int probe_error;
} PCI_BAR_INFO, *PPCI_BAR_INFO;
typedef struct _PCI_DEV_INFO {
    BYTE bus,dev,func;
    BYTE hdr_type;
    WORD vendor,device;
    BYTE class_code,subclass,prog_if,revision;
    WORD command,status; // capture
    BYTE irq_line,irq_pin;
    WORD subsys_vendor,subsys_device;
    BYTE subsys_valid;
    BYTE bar_count;
    PCI_BAR_INFO bars[6];
    BYTE cap_ptr;
} PCI_DEV_INFO, *PPCI_DEV_INFO;
PPCI_DEV_INFO ptr_devs; // 设备信息数组
DWORD found_devs;
const char *pci_class_names(BYTE cls, BYTE sub) {
	switch (cls) {
	case 0x00: return "UNCLASSFIED\0";
	case 0x01:
		switch (sub) {
		case 0x01: return "STORAGE IDE\0";
		case 0x06: return "STORAGE SATA/AHCI\0";
		case 0x08: return "STORAGE NVME\0";
		}
		return "STORAGE\0";
	case 0x02:
		return sub == 0x00 ? "NET ETH\0" :
		       sub == 0x80 ? "NET OTH\0" : "NET\0";
	case 0x03:
		return sub == 0x00 ? "GFX VGA\0" : "GFX\0";
	case 0x04:
		switch (sub) {
		case 0x00: return "AUDIO AC97\0";
		case 0x01: return "AUDIO MIDI\0";
		case 0x03: return "AUDIO HDA\0";
		}
		return "MULTIMEDIA\0";
	case 0x05: return "MEM CTRL\0";
	case 0x06:
		switch (sub) {
		case 0x00: return "BRIDGE HOST\0";
		case 0x01: return "BRIDGE ISA\0";
		case 0x04: return "BRIDGE PCI2PCI\0";
		}
		return "BRIDGE\0";
	case 0x07: return "SERIAL/COMM\0";
	case 0x08: return "SYS EXT\0";
	case 0x09: return "INPUT\0";
	case 0x0B: return "CPU\0";
	case 0x0C:
		switch (sub) {
		case 0x03: return "USB\0";
		case 0x05: return "SMBUS\0";
		}
		return "SBUS\0";
	case 0x0D: return "WIRELESS\0";
	case 0x11: return "SIGNAL PROC\0";
	}
	return "DEVICE\0";
}
const char *pci_vendor_names(WORD ven) {
	switch (ven) {
	case 0x8086: return "INTEL\0";
	case 0x1002: return "AMD/ATI\0";
	case 0x1022: return "AMD\0";
	case 0x10DE: return "NVIDIA\0";
	case 0x1234: return "QEMU\0";
	case 0x1AF4: return "VIRTIO\0";
	case 0x1B36: return "RED HAT\0";
	case 0x10EC: return "REALTEK\0";
	case 0x14E4: return "BROADROM\0";
	case 0x1969: return "ATHEROS\0";
	case 0x1106: return "VIA\0";
	case 0x5333: return "S3\0";
	case 0x80EE: return "VIRTUALBOX\0";
	case 0x15AD: return "VMWARE\0";
	}
	return 0;
}
static void proc_pl_scan() {
    DWORD scan_ptr;
    found_devs=0;
    if (ptr_devs) {
        kfree(ptr_devs);
        ptr_devs=0;
    }
    for (DWORD bus=0;bus<256;bus++) {
        for (DWORD dev=0;dev<32;dev++) {
            BYTE nfn=1;
            for (BYTE fn=0;fn<nfn;fn++) {
                // VENDOR ID -> +0:0000ffffh
                WORD ven=lw_pci_read((BYTE)bus,dev,fn,0)&0xffff;
                BYTE hdr;
                if (ven==0xffff||ven==0)continue;
                if (fn==0) {
                    // HEADER TYPE -> +c:00ff0000h
                    hdr=(lw_pci_read((BYTE)bus,dev,fn,0xc)>>16)&0xff;
                    if (hdr&0x80)nfn=8;
                }
                found_devs++;
            }
        }
    }
    if (found_devs==0)return;
    ptr_devs=kmalloc(found_devs*sizeof(PCI_DEV_INFO));
    scan_ptr=0;
    for (DWORD bus=0;bus<256;bus++) {
        for (DWORD dev=0;dev<32;dev++) {
            BYTE nfn=1;
            for (BYTE fn=0;fn<nfn;fn++) {
                // VENDOR ID -> +0:0000ffffh
                WORD ven=lw_pci_read((BYTE)bus,dev,fn,0)&0xffff;
                // DEVICE -> +0:ffff0000h
                WORD device=(lw_pci_read((BYTE)bus,dev,fn,0)>>16)&0xffff;
                WORD cmd, stt;
                WORD subsysven, subsysdev;
                BYTE cls, sub, hdr, pif, rev;
                BYTE irql,irqp;
                if (ven==0xffff||ven==0)continue;
                if (fn==0) {
                    // HEADER TYPE -> +c:00ff0000h
                    hdr=(lw_pci_read((BYTE)bus,dev,fn,0xc)>>16)&0xff;
                    if (hdr&0x80)nfn=8;
                }
                // STATUS -> +4:ffff0000h
                stt=(lw_pci_read((BYTE)bus,dev,fn,4)>>16)&0xffff;
                // COMMAND -> +4:0000ffffh
                cmd=lw_pci_read((BYTE)bus,dev,fn,4)&0xffff;
                // CLASS -> +8:ff000000h
                cls=(lw_pci_read((BYTE)bus,dev,fn,8)>>24)&0xff;
                // SUBCLASS -> +8:00ff0000h
                sub=(lw_pci_read((BYTE)bus,dev,fn,8)>>16)&0xff;
                // PROG IF -> +8:0000ff00h
                pif=(lw_pci_read((BYTE)bus,dev,fn,8)>>8)&0xff;
                // REVISION -> +8:000000ffh
                rev=lw_pci_read((BYTE)bus,dev,fn,8)&0xff;
                // SUBSYS VENDER -> +2c:ffff0000h
                subsysven=(lw_pci_read((BYTE)bus,dev,fn,0x2c)>>16)&0xffff;
                // SUBSYS DEVICE -> +2c:0000ffffh
                subsysdev=lw_pci_read((BYTE)bus,dev,fn,0x2c)&0xffff;
                // IRQPIN -> +3c:0000ff00h
                irqp=(lw_pci_read((BYTE)bus,dev,fn,0x3c)>>8)&0xff;
                // IRQLINE -> +3c:000000ffh
                irql=(lw_pci_read((BYTE)bus,dev,fn,0x3c)&0xff);
                ptr_devs[scan_ptr].bus=bus;
                ptr_devs[scan_ptr].dev=dev;
                ptr_devs[scan_ptr].func=fn;
                ptr_devs[scan_ptr].hdr_type=hdr;
                ptr_devs[scan_ptr].vendor=ven;
                ptr_devs[scan_ptr].device=device;
                ptr_devs[scan_ptr].class_code=cls;
                ptr_devs[scan_ptr].subclass=sub;
                ptr_devs[scan_ptr].prog_if=pif;
                ptr_devs[scan_ptr].revision=rev;
                ptr_devs[scan_ptr].status=stt;
                ptr_devs[scan_ptr].command=cmd;
                ptr_devs[scan_ptr].irq_line=irql;
                ptr_devs[scan_ptr].irq_pin=irqp;
                ptr_devs[scan_ptr].subsys_vendor=subsysven;
                ptr_devs[scan_ptr].subsys_device=subsysdev;
                ptr_devs[scan_ptr].bar_count=0;
                for (int i=0; i<6; i++) {
                    BYTE bar_slots;
                    switch (hdr&3) {
                        case 0:bar_slots=6;break;
                        case 1:bar_slots=2;break;
                        case 2:bar_slots=0;break;
                        default: {
                            // ...
                            break;
                        }
                    }
                    BYTE off=(BYTE)(0x10+i*4);
                    DWORD v=lw_pci_read(bus,dev,fn,off);
                    ptr_devs[scan_ptr].bars[i].slot=i;
                    ptr_devs[scan_ptr].bars[i].slots=1;
                    ptr_devs[scan_ptr].bars[i].size=0;
                    ptr_devs[scan_ptr].bars[i].size_state=BAR_SIZE_UNKNOWN;
                    ptr_devs[scan_ptr].bars[i].probe_error=0;

                    if (v&1) {
                        ptr_devs[scan_ptr].bars[i].kind=BAR_IO;
                        ptr_devs[scan_ptr].bars[i].base=v&0xfffffffcu;
                        ptr_devs[scan_ptr].bars[i].prefetchable=0;
                    } else {
                        BYTE type=(v>>1)&3;
                        ptr_devs[scan_ptr].bars[i].prefetchable=!!(v&8);
                        if (type==0) {
                            ptr_devs[scan_ptr].bars[i].kind=BAR_MEM32;
                            ptr_devs[scan_ptr].bars[i].base=v&0xfffffff0u;
                        } else if (type==2) {
                            if (i+1>=bar_slots) {
                                break;
                            }
                            DWORD h=v=lw_pci_read(bus,dev,fn,off+4);
                            ptr_devs[scan_ptr].bars[i].kind=BAR_MEM64;
                            ptr_devs[scan_ptr].bars[i].slots=2;
                            ptr_devs[scan_ptr].bars[i].base=
                                ((QWORD)h<<32)|(v&0xfffffff0);
                        } else {
                            break;
                        }
                    }
                    ptr_devs[scan_ptr].bar_count+=bar_slots;
                }
                scan_ptr++;
            }
        }
    }
}
static void proc_pl_detect(DWORD index) {

}
static void proc_pi(PTUI_AREA a, DWORD index) {
    set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
    putxy("VENDOR",a->x+1,a->y+1);
    putxy("CLASS",a->x+1,a->y+3);
    set_attr(TUI_WHITE,TUI_BLUE);
    putxy(pci_vendor_names(ptr_devs[index].vendor),a->x+1,a->y+2);
    putxy(pci_class_names(ptr_devs[index].class_code,ptr_devs[index].subclass),
        a->x+1,a->y+4);
}
static void proc_pl(PTUI_AREA a, DWORD ctrl) {
    CHAR tstrdw[9];
    CHAR tstrqw[17];
    CHAR text[256];
    DWORD scan_ptr;
    static DWORD index;

    switch (ctrl) {
        case VK_UP: {
            index--;
            if (index<0)index=0;
            break;
        }
        case VK_DOWN: {
            index++;
            if (index>found_devs)index=found_devs;
            break;
        }
        case VK_F5: {
            proc_pl_scan();
            break;
        }
        case VK_F6: {
            proc_pl_detect(index);
            break;
        }
    }
    if (focus!=a->index)goto draw;
    //  012345678901234567
    // "BB:DD:F VVVV:CCCC"
    proc_pi(&area_pci_info,index);
    for (DWORD i=0; i<found_devs; i++) {
        if (i==index) {
            if (focus==a->index) {
                set_attr(TUI_BLACK,TUI_WHITE);
            } else {
                set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
            }
        } else {
            set_attr(TUI_WHITE,TUI_BLUE);
        }
        hex_xlat(ptr_devs[i].bus,text,2);
        text[2]=':';
        hex_xlat(ptr_devs[i].dev,text+3,2);
        text[5]=':';
        hex_xlat(ptr_devs[i].func,text+6,1);
        text[7]=' ';
        hex_xlat(ptr_devs[i].vendor,text+8,4);
        text[12]=':';
        hex_xlat(ptr_devs[i].device,text+13,4);
        text[17]=0;
        putxy(text,a->x+1,a->y+1+i);
    }
    set_attr(TUI_WHITE|TUI_LIGHT,TUI_BLUE);
    putxy("UP/DOWN=DEVICES",0,23);
    putxy("F5=REFRESH      F6=DETECT",0,24);
    draw:
}
void tui_init() {
    video=(PVOID)0xb8000;
    subvideo=(PWORD)kmalloc(80*25*sizeof(WORD));
    if (subvideo==(PVOID)-1 || subvideo==(PVOID)-2) {
        set_attr(TUI_WHITE|TUI_LIGHT,TUI_RED);
        putxy("OUT OF MEMORY DURING ALLOCATE SUBVIDEO BUFFER",10,20);
        lw_getk();
    }
    focus=0;
    exe_file_buf=0;
    ptr_devs=0;
    for (int i=0; i<80*25; i++)video[i]=0x1020;

    area_mem_reader.x=6;
    area_mem_reader.y=1;
    area_mem_reader.w=32;
    area_mem_reader.h=9;
    area_mem_reader.name=name_area_mr;
    area_mem_reader.index=1;

    area_mem_dump.x=6;
    area_mem_dump.y=11;
    area_mem_dump.w=44;
    area_mem_dump.h=11;
    area_mem_dump.name=name_area_md;
    area_mem_dump.index=2;

    area_file_info.x=40;
    area_file_info.y=1;
    area_file_info.w=10;
    area_file_info.h=9;
    area_file_info.name=name_area_fi;
    area_file_info.index=-1;

    area_file_expl.x=52;
    area_file_expl.y=1;
    area_file_expl.w=27;
    area_file_expl.h=21;
    area_file_expl.name=name_area_fe;
    area_file_expl.index=3;

    area_pci_list.x=6;
    area_pci_list.y=1;
    area_pci_list.w=18;
    area_pci_list.h=20;
    area_pci_list.name=name_area_pl;
    area_pci_list.index=1;

    area_pci_info.x=25;
    area_pci_info.y=1;
    area_pci_info.w=20;
    area_pci_info.h=10;
    area_pci_info.name=name_area_pi;
    area_pci_info.index=-1;

    area_mem_pool.x=6;
    area_mem_pool.y=1;
    area_mem_pool.w=72;
    area_mem_pool.h=20;
    area_mem_pool.name=name_area_mp;
    area_mem_pool.index=-1;

    proc_pl_scan();

    fe_current_cluster=monitor_fs.root_cluster;
    strcpy(fe_path, "/");
    fe_index=0;
    proc_fe_update_list();

    menu_index=2;

    lw_setcur(0);
}
void tui_clr() {
    for (int i=0; i<80; i++) {
        video[i]=(((WORD)TUI_WHITE)<<4)<<8;
    }
    set_attr(TUI_BLACK,TUI_WHITE);
    putxy("LWOS MONITOR V1 TUI",2,0);
    set_attr(TUI_WHITE,TUI_BLUE);
    for (int i=0;i<160;i++) {
        video[80*23+i]=((WORD)vi_attr)<<8;
    }
}
void tui_draws(void);
void tui_procs(DWORD t) {
    if (hex_active) {
        proc_hex(t);
        if (hex_active)return;
        t=0;
        draw_menu();
        tui_draws();
    }
    tui_clr();
    proc_menu(t);
    switch (tuim_page) {
        case 0: {
            proc_mr(&area_mem_reader,t);
            proc_md(&area_mem_dump,t);
            proc_fe(&area_file_expl,t);
            if (hex_active) hex_draw();
            break;
        }
        case 1: {
            proc_pl(&area_pci_list,t);
            break;
        }
        case 3: {
            proc_mp(&area_mem_pool);
            break;
        }
    }
}
void tui_draws() {
    if (hex_active)return;
    switch (tuim_page) {
        case 0: {
            draw_area(&area_mem_reader);
            draw_area(&area_mem_dump);
            draw_area(&area_file_info);
            draw_area(&area_file_expl);
            break;
        }
        case 1: {
            draw_area(&area_pci_list);
            draw_area(&area_pci_info);
            break;
        }
        case 2: {
            draw_area(&area_mem_pool);
            break;
        }
    }
}
void tui_loop() {
    DWORD t=0;
    rm_alarm();
    tui_draws();
    while (1) {
        switch (hex_active ? 0 : t) {
            case VK_TAB: {
                focus++;
                focus%=4;
                break;
            }
        }
        for (int i=0; i<80*25; i++) {
            video[i]=0x1020;
        }
        tui_draws();
        tui_procs(t);
        if (!hex_active)draw_menu();
        t=lw_getk();
    }
}