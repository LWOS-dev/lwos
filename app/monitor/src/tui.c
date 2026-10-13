#include "../include/tui.h"
#include "../include/keys.h"

#include "abi.h"
#include "string.h"
#include "convert.h"
#include "mem.h"

PWORD video;
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

const char name_units[4][6]={" BYTE"," WORD","DWORD","QWORD"};
const char name_area_mr[]="MEM READER";
const char name_area_md[]="MEM DUMP";

void draw_area(PTUI_AREA p) {
    set_attr((focus==p->index)?(TUI_YELLOW|TUI_LIGHT):(TUI_WHITE),TUI_BLUE);
    for (int i=0; i<p->w; i++) {
        putxyc(196,p->x+i,p->y);
        putxyc(196,p->x+i,p->y+p->h);
    }
    for (int i=0; i<p->h; i++) {
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
    putxy(p->name,p->x+1,p->y);
}
static int type_ptr=0;
static CHAR type_buf[17];
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
            offset[index]-=64;
            break;
        }
        case VK_PGDN: {
            offset[index]+=64;
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
void tui_init() {
    video=(PVOID)0xb8000;
    focus=0;
    for (int i=0; i<80*25; i++) {
        video[i]=(TUI_BLUE<<4)<<8;
    }
    for (int i=0; i<80; i++) {
        video[i]=(TUI_WHITE<<4)<<8;
    }
    lw_setcur(0);
    set_attr(TUI_BLACK,TUI_WHITE);
    putxy("LWOS MONITOR V1 TUI",2,0);
    set_attr(TUI_WHITE,TUI_BLUE);
    for (int i=0;i<160;i++) {
        video[80*22+i]=vi_attr<<8;
    }

    area_mem_reader.x=0;
    area_mem_reader.y=1;
    area_mem_reader.w=32;
    area_mem_reader.h=9;
    area_mem_reader.name=name_area_mr;
    area_mem_reader.index=0;

    area_mem_dump.x=0;
    area_mem_dump.y=11;
    area_mem_dump.w=44;
    area_mem_dump.h=11;
    area_mem_dump.name=name_area_md;
    area_mem_dump.index=1;
}
void tui_loop() {
    DWORD t=0;
    while (1) {
        set_attr(TUI_WHITE,TUI_BLUE);
        for (int i=0;i<80;i++) {
            putxyc(' ',i,23);
            putxyc(' ',i,24);
        }
        switch (t) {
            case VK_TAB: {
                focus++;
                focus%=4;
                break;
            }
        }
        draw_area(&area_mem_reader);
        draw_area(&area_mem_dump);
        proc_mr(&area_mem_reader,t);
        proc_md(&area_mem_dump,t);
        t=lw_getk();
    }
}
