#include "eth_tmp.h"
#include "net.h"
#include "mem.h"

static LWFT_HDR header;
static PLWFT_HDR lwft_p=&header;
const char lwft_hdr_opcode[][10] = {
    "OP_HELLO\0",
    "OP_PUT\0",
    "OP_DATA\0",
    "OP_END\0",
    "OP_ACK\0",
    "OP_NAK\0",
    "OP_RUN\0",
    "OP_TEXT\0"
};
static int hex_catch(char c) {
    switch (c) {
        case '0':return 0x0;
        case '1':return 0x1;
        case '2':return 0x2;
        case '3':return 0x3;
        case '4':return 0x4;
        case '5':return 0x5;
        case '6':return 0x6;
        case '7':return 0x7;
        case '8':return 0x8;
        case '9':return 0x9;
        case 'a':case 'A':return 0xa;
        case 'b':case 'B':return 0xb;
        case 'c':case 'C':return 0xc;
        case 'd':case 'D':return 0xd;
        case 'e':case 'E':return 0xe;
        case 'f':case 'F':return 0xf;
        default:return -1u;
    }
}
static DWORD parse_dw_str(char* p) {
    DWORD r=0;
    for (int i=0;i<8;i++) {
        r=r<<4;
        r|=hex_catch(p[i]);
    }
    return r;
}
void on_frame(PVOID buf, int len) {
    if (len<30)return;
    char *p=0;
    DWORD op1,op2;
    WORD type=((*((PBYTE)buf+12))<<8)|(*((PBYTE)buf+13));
    if (type!=0x88b5) {
        return;
    }
    memcpy(lwft_p,(PBYTE)buf+14,sizeof(*lwft_p));
    if (lwft_p->magic != LWFT_MAGIC || lwft_p->op<OP_HELLO || lwft_p->op>OP_TEXT ||
        lwft_p->len>LWFT_MAX_DATA || lwft_p->len>(unsigned)(len-30)) {
        return;
    }
    lw_puts("OP:");
    lw_puts(lwft_hdr_opcode[lwft_p->op-1]);
    lw_puts(" FLAG:");
    lw_put_byte(lwft_p->flags);
    lw_puts("\n\rLEN:");
    lw_put_word(lwft_p->len);
    lw_puts(" SEQ:");
    lw_put_dword(lwft_p->seq);
    lw_puts(" ARG:");
    lw_put_dword(lwft_p->arg);
    lw_puts("\n\r");
    p=(char*)((PBYTE)buf+30);
    switch (lwft_p->op) {
        case OP_HELLO: {
            lw_puts("HELLO\n\r");
            break;
        }
        case OP_RUN: {
            if (!lwft_p->len)break;
            switch (p[0]) {
                case 'w':case 'W': {
                    if (lwft_p->len<17)break;
                    op1=parse_dw_str(p+1)&0xfffffffc;
                    op2=parse_dw_str(p+9);
                    lw_puts("CMD: WRITE [");
                    lw_put_dword(op1);
                    lw_puts("]=");
                    lw_put_dword(op2);
                    *(volatile PDWORD)(op1)=op2;
                    if (*((volatile PDWORD)(op1))!=op2) {
                        lw_puts(" (DIFF) [");
                        lw_put_dword(op1);
                        lw_puts("]=");
                        lw_put_dword(*(volatile PDWORD)(op1));
                    }
                    lw_puts("\n\r");
                    break;
                }
                case 'r':case 'R': {
                    if (lwft_p->len<9)break;
                    op1=parse_dw_str(p+1)&0xfffffffc;
                    lw_puts("CMD: READ [");
                    lw_put_dword(op1);
                    lw_puts("]=");
                    lw_put_dword(*(volatile PDWORD)(op1));
                    lw_puts("\n\r");
                    break;
                }
            }
            break;
        }
        case OP_TEXT: {
            lw_puts_pad((const char*)((PBYTE)buf+30),lwft_p->len);
            break;
        }
    }
}
/* The temporary monitor console now borrows resman's IRQ endpoint too. */
int eth_init(void) {
    BYTE mac[6];
    if (!rm_base)return RM_ERR_NOT_READY;
    int rc=rm_net_open(mac);
    LW_NET_INFO info;
    if (!rm_net_info(&info)) {
        lw_puts("NET PCI=");lw_put_dword(info.pci_id);
        lw_puts(" IRQ=");lw_put_dword(info.irq);
        lw_puts(" STATUS=");lw_put_dword(info.status);
        lw_puts(" PROBLEM=");lw_put_dword(info.problem);
        lw_puts("\n\r");
    }
    return rc;
}
int eth_receive(void) {
    BYTE frame[LW_NET_FRAME_MAX];
    int size=rm_net_recv(frame,sizeof(frame),100);
    if (size>0)on_frame(frame,size);
    return size;
}
void eth_close(void) {if (rm_base)rm_net_close();}
