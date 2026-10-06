#include "eth_tmp.h"

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818

#define RX_N       8
#define RX_RING    0x00303000
#define RX_BUF     0x00304000

BYTE eth_b, eth_d, eth_f;
DWORD eth_bar0;
DWORD rx_cur;

#define R(off) read_reg(eth_bar0+(off))
#define W(off, v) write_reg(eth_bar0+(off), (v))

void get_eth_bdf(PBYTE b, PBYTE d, PBYTE f) {
    *b=eth_b;
    *d=eth_d;
    *f=eth_f;
}
void set_eth_bdf(BYTE b, BYTE d, BYTE f) {
    eth_b=b;
    eth_d=d;
    eth_f=f;
}
void set_eth_bar(DWORD bar) {
    eth_bar0=bar;
    lw_puts("PRETHERNET: BAR0 VERITIED TO ");
    lw_put_dword(eth_bar0);
    lw_puts("\n\r");
}
static int write_reg(DWORD address, DWORD value) {
    *(PDWORD)(address&0xfffffffc)=value;
    if ((*(PDWORD)(address&0xfffffffc))==value) {
        return 1;
    } else {
        return 0;
    }
}
static DWORD read_reg(DWORD address) {
    return *(PDWORD)(address&0xfffffffc);
}
#define LW_ETHERTYPE    0x88B5
#define LWFT_MAGIC      0x5446574Cu     /* 'LWFT' 小端 */
#define LWFT_MAX_DATA   1024
PLWFT_HDR lwft_p=0;
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
    (void)len;
    char *p=0;
    DWORD op1,op2;
    WORD type=((*((PBYTE)buf+12))<<8)|(*((PBYTE)buf+13));
    if (type!=0x88b5) {
        return;
    }
    lwft_p=(PLWFT_HDR)(buf+14);
    if (lwft_p->magic != LWFT_MAGIC) {
        return;
    }
    lw_puts("OP:");
    lw_puts(lwft_hdr_opcode[lwft_p->op]);
    lw_puts(" FLAG:");
    lw_put_byte(lwft_p->flags);
    lw_puts("\n\rLEN:");
    lw_put_word(lwft_p->len);
    lw_puts(" SEQ:");
    lw_put_dword(lwft_p->seq);
    lw_puts(" ARG:");
    lw_put_dword(lwft_p->arg);
    lw_puts("\n\r");
    p=(char*)((PBYTE)lwft_p+16);
    switch (lwft_p->op) {
        case OP_HELLO: {
            lw_puts("HELLO\n\r");
            break;
        }
        case OP_RUN: {
            switch (p[0]) {
                case 'w':case 'W': {
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
            lw_puts_pad((const char*)((PBYTE)lwft_p+16),lwft_p->len);
            break;
        }
    }
}
int e1k_rx_poll(void) {
    unsigned long d = RX_RING + rx_cur * 16;
    unsigned long st = read_reg(d + 12);
    if (!(st & 1))                               /* DD 为 0，没有新帧 */
        return 0;

    unsigned len = read_reg(d + 8) & 0xFFFF;
    unsigned err = (st >> 8) & 0xFF;
    if (err == 0) {
        //lw_dump128((PVOID)(RX_BUF + rx_cur * 2048));
        on_frame((PVOID)(RX_BUF+rx_cur*2048), len);
    }

    write_reg(d + 12, 0);                        /* 清状态 */
    W(REG_RDT, rx_cur);                          /* 把这个描述符还给硬件 */
    rx_cur = (rx_cur + 1) % RX_N;
    return 1;
}
void eth_init(void) {
    lw_puts("ETHERNET INIT\n\r");
    lw_puts("DEVICE FOUND AT ");
    lw_puts("BUS ");
    lw_put_byte(eth_b);
    lw_puts(" DEV ");
    lw_put_byte(eth_d);
    lw_puts(" FUNC ");
    lw_put_byte(eth_f);
    lw_puts("\n\r");

    W(REG_IMC,0xffffffff);
    (void)R(REG_ICR);
    W(REG_CTRL, R(REG_CTRL) | 0x40); 

    W(REG_RCTL, 0);
    for (unsigned i = 0; i < RX_N; i++) {
        unsigned long d = RX_RING + i * 16;
        write_reg(d + 0,  RX_BUF + i * 2048);
        write_reg(d + 4,  0);
        write_reg(d + 8,  0);
        write_reg(d + 12, 0);
    }
    W(REG_RDBAL, RX_RING);
    W(REG_RDBAH, 0);
    W(REG_RDLEN, RX_N * 16);
    W(REG_RDH, 0);
    W(REG_RDT, RX_N - 1);
    rx_cur = 0;
    W(REG_RCTL, 0x0400801A);
}