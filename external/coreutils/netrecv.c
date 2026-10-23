#include "stdio.h"
#include "abi.h"
#include "crt.h"
#include "fs.h"
#include "path.h"
#include "net.h"
#include "lwft.h"
#include "mem.h"
#include "time.h"
#include "string.h"

#define TRANSFER_TIMEOUT 30000u
#define END_LINGER 2000u
typedef struct {
    BYTE mac[6],peer[6];
    DWORD total,received,next_seq,crc,last_hash,last_seq;
    DWORD frames,replies,reply_errors;
    int started,complete;
} TRANSFER;
static int same_peer(PCBYTE a,PCBYTE b) {
    for (unsigned i=0;i<6;i++)if (a[i]!=b[i])return 0;
    return 1;
}
static void print_net_info(void) {
    static const char *problems[]={"ready","no supported device","invalid BAR0","PCI D0/command failed",
        "invalid PIC IRQ route","IRQ already occupied","firmware/SWFLAG busy","PHY MDIO failed",
        "invalid MAC address","MAC reset timeout","INTx self-test failed","endpoint busy",
        "DMA master stop timeout","NVM auto-load timeout"};
    LW_NET_INFO n;
    if (rm_net_info(&n))return;
    printf("net: PCI %04x:%04x at %02x:%02x.%u BAR=%08x IRQ=%u\n",
           n.pci_id&0xffff,n.pci_id>>16,n.bdf>>16,(n.bdf>>8)&0xff,n.bdf&7,n.bar,n.irq);
    printf("net: %s; LINK=%s INTx-test=%s CMD=%04x PMCSR=%08x\n",
           n.problem<sizeof(problems)/sizeof(problems[0]) ? problems[n.problem] : "unknown",
           !n.mmio_ready ? "UNKNOWN" : (n.status&2) ? "UP" : "DOWN",
           n.irq_test ? "PASS" : "FAIL",n.command,n.pmcsr);
    printf("net: CTRL=%08x STATUS=%08x RAL=%08x RAH=%08x\n",n.ctrl,n.status,n.ral,n.rah);
    if (n.pci_id==0x10f58086u)
        printf("net: FWSM=%08x EXTCNF=%08x PHY_CTRL=%08x BMCR=%04x\n",
               n.fwsm,n.extcnf,n.phy_ctrl,n.bmcr);
    printf("net: IRQs=%u RX=%u DROP=%u ICR=%08x RXDESC=%08x\n",
           n.irq_count,n.rx_count,n.dropped,n.last_icr,n.rx_pending);
    printf("net: TX queued=%u done=%u busy=%u TDH=%u TDT=%u DESC=%08x\n",
           n.tx_posted,n.tx_done,n.tx_busy,n.tdh,n.tdt,n.tx_desc);
    printf("net: TCTL=%08x TXDCTL=%08x TARC0=%08x\n",n.tctl,n.txdctl,n.tarc0);
    printf("net: TCTL-reset=%08x TARC1=%08x\n",n.tctl_reset,n.tarc1);
    printf("net: TDBAH=%08x TDBAL=%08x TDLEN=%u ring=%08x\n",
           n.tdbah,n.tdbal,n.tdlen,n.tx_ring);
    printf("net: CTRL_EXT=%08x EEC=%08x PBA=%08x PBS=%08x\n",
           n.ctrl_ext,n.eec,n.pba,n.pbs);
    printf("net: TX0 addr=%08x cmdlen=%08x status=%08x\n",
           n.tx0_addr,n.tx0_info,n.tx0_status);
}

static void reply(TRANSFER *t,BYTE op,DWORD seq,DWORD arg) {
    BYTE frame[60]={0};
    memcpy(frame,t->peer,6);memcpy(frame+6,t->mac,6);
    frame[12]=0x88;frame[13]=0xb5;
    LWFT_HDR header={LWFT_MAGIC,op,0,0,seq,arg};
    memcpy(frame+14,&header,sizeof(header));
    /* A full TX ring loses only this ACK; the sender retransmits the request. */
    int rc=rm_net_send(frame,sizeof(frame));
    if (!rc)t->replies++;
    else if (!t->reply_errors++)printf("netrecv: reply queue failed: %d\n",rc);
}
static int receive_file(PCSTR path) {
    TRANSFER t={.crc=0xffffffffu};
    FS_VOLUME volume={0};FS_FILE file={0};
    BYTE frame[LW_NET_FRAME_MAX];
    int rc=rm_net_open(t.mac);
    print_net_info();
    if (rc) {printf("netrecv: endpoint unavailable or busy (%d)\n",rc);return rc;}
    printf("netrecv: waiting for LWFT -> %s\n",path);
    DWORD progress=time_now_ms();
    for (;;) {
        DWORD limit=t.complete ? END_LINGER : TRANSFER_TIMEOUT;
        DWORD elapsed=time_elapsed_ms(progress);
        if (elapsed>=limit) {rc=t.complete ? 0 : RM_ERR_TIMEOUT;break;}
        int size=rm_net_recv(frame,sizeof(frame),limit-elapsed);
        if (size<0) {rc=size;break;}
        if (size>0)t.frames++;
        if (size<30 || frame[12]!=0x88 || frame[13]!=0xb5)continue;
        LWFT_HDR h;memcpy(&h,frame+14,sizeof(h));
        if (h.magic!=LWFT_MAGIC || h.flags || h.len>LWFT_MAX_DATA ||
            (unsigned)size<30u+h.len || (frame[6]&1))continue;
        BYTE *data=frame+30;
        DWORD hash=lwft_crc32(0xffffffffu,frame+14,16+h.len);
        if (!t.started) {
            if (h.op!=OP_PUT || h.seq || !h.len || h.len>255)continue;
            memcpy(t.peer,frame+6,6);
            /* The sender's filename is metadata only; only the local argument
             * chooses the destination. No network-supplied path is executed.
             */
            rc=fs_mount(&volume,lw_blk_get(0));
            if (!rc)rc=fs_touch(&volume,path);
            if (!rc)rc=fs_file_open(&volume,path,&file);
            if (!rc)rc=fs_file_truncate(&file);
            if (rc) {reply(&t,OP_NAK,h.seq,rc);break;}
            t.started=1;t.total=h.arg;t.next_seq=1;
            printf("netrecv: PUT accepted, total=%u; sending ACK seq=0\n",t.total);
        } else {
            if (!same_peer(frame+6,t.peer))continue;
            if (h.seq==t.last_seq && hash==t.last_hash) {
                reply(&t,OP_ACK,h.seq,t.received);continue;
            }
            if (t.complete)continue;
            if (h.seq!=t.next_seq) {reply(&t,OP_NAK,h.seq,RM_ERR_ARGUMENT);continue;}
            if (h.op==OP_DATA && h.len && h.arg==t.received && h.len<=t.total-t.received) {
                DWORD written=0;
                rc=fs_file_write(&file,data,h.len,&written);
                if (!rc && written!=h.len)rc=FS_ERR_IO;
                if (rc) {reply(&t,OP_NAK,h.seq,rc);break;}
                t.crc=lwft_crc32(t.crc,data,h.len);t.received+=h.len;t.next_seq++;
            } else if (h.op==OP_END && !h.len && t.received==t.total) {
                if (h.arg!=(t.crc^0xffffffffu)) {
                    rc=RM_ERR_FORMAT;reply(&t,OP_NAK,h.seq,rc);break;
                }
                rc=fs_file_close(&file);
                if (rc) {reply(&t,OP_NAK,h.seq,rc);break;}
                t.complete=1;
                printf("netrecv: saved %u bytes, CRC32 %08x\n",t.received,t.crc^0xffffffffu);
            } else {reply(&t,OP_NAK,h.seq,RM_ERR_ARGUMENT);continue;}
        }
        t.last_seq=h.seq;t.last_hash=hash;progress=time_now_ms();
        reply(&t,OP_ACK,h.seq,t.received);
    }
    if (rc) {
        printf("netrecv: stage=%s next-seq=%u bytes=%u/%u frames=%u replies=%u failed=%u\n",
               t.started ? "DATA/END" : "PUT",t.next_seq,t.received,t.total,t.frames,t.replies,t.reply_errors);
        print_net_info();
    }
    fs_file_close(&file);
    int close_rc=rm_net_close();
    if (!rc)rc=close_rc;
    return rc;
}
int main(int argc,char **argv) {
    if (argc!=2 || !argv[1] || !*argv[1]) {
        printf("Usage: netrecv FILE | netrecv -d (initialize and diagnose)\n");return RM_ERR_ARGUMENT;
    }
    if (!strcmp(argv[1],"-d")) {
        BYTE mac[6];int rc=rm_net_open(mac);
        if (!rc) {
            /* Auto-negotiation can take seconds after power-up. This is a
             * bounded sleeping link check; RX remains interrupt-driven.
             */
            for (unsigned i=0;i<30;i++) {
                LW_NET_INFO n;
                if (rm_net_info(&n) || (n.status&2))break;
                rm_sleep(100);
            }
        }
        print_net_info();
        return rc ? rc : rm_net_close();
    }
    char path[256];
    int rc=lw_path_resolve(lw_env_cwd(lw_env),argv[1],path,sizeof(path));
    if (!rc)rc=receive_file(path);
    if (rc)printf("netrecv: error %d (incomplete file may remain)\n",rc);
    return rc;
}
