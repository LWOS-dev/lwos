#include "../include/net.h"
#include "../include/task.h"
#include "../include/pit.h"
#include "abi.h"
#include "mem.h"
#include "../include/resman.h"

/* Intel legacy descriptors, identity-mapped coherent DMA on i386.
 * Rings and buffers live for the lifetime of resman, never in application RAM.
 * Intel 8254x SDM sections 3.2/3.3/13.4; RX is drained only in net_irq().
 */
#define N 8u
/* ICH SDM 10.4.58 requires >=16 TX descriptors, even though QEMU's
 * 82540 accepts eight. RX and the software receive queue stay independent.
 */
#define TX_N 16u
#define CTRL 0x0000
#define STATUS 0x0008
#define ICR 0x00c0
#define IMS 0x00d0
#define IMC 0x00d8
#define RCTL 0x0100
#define TCTL 0x0400
#define TCTL_EN 2u
#define TCTL_MULR (1u<<28)
#define RX_IRQS 0xd0u /* RXDMT0, RXO, RXT0 */
#define CTRL_EXT 0x0018
#define MDIC 0x0020
#define EXTCNF 0x0f00
#define PHY_CTRL 0x0f10
#define FWSM 0x5b54
#define SWFLAG 0x20u
#define DRV_LOAD (1u<<28)
typedef struct {DWORD addr,high,info,status;} DESC;
static volatile DESC rx[N] __attribute__((aligned(128)));
static volatile DESC tx[TX_N] __attribute__((aligned(128)));
static BYTE rx_buffers[N][2048] __attribute__((aligned(16)));
static BYTE tx_buffers[TX_N][2048] __attribute__((aligned(16)));
_Static_assert(TX_N>=16 && (TX_N*sizeof(DESC))%128==0,"invalid ICH TX ring size");
static struct {DWORD length;BYTE data[LW_NET_FRAME_MAX];} queue[N];
static DWORD bar,irq,owner=0xffffffffu,rx_cur,tx_cur,head,tail;
static BYTE bus,dev,fn,elcr_saved;
static int active,waiting,opening,mmio_ready,swflag_owned,irq_installed;
static int driver_claimed;
static DWORD old_drv_load;
static LW_NET_INFO info;
static volatile DWORD irq_test;
static void shutdown(void);
static DWORD deadline;
volatile DWORD net_irq_count,net_rx_count,net_dropped;
static DWORD irq_total;
static DWORD tx_clean,tx_posted,tx_done,tx_busy;
static int reset_quiet,shutdown_pending;
static DWORD mmio_quiet_until;

#ifdef LW_NET_TEST
extern DWORD net_test_read_reg(DWORD off);
extern void net_test_write_reg(DWORD off,DWORD value);
#define read_reg net_test_read_reg
#define write_reg net_test_write_reg
#else
static DWORD read_reg(DWORD off) {return *(volatile DWORD *)(bar+off);}
static void write_reg(DWORD off,DWORD value) {*(volatile DWORD *)(bar+off)=value;}
#endif
static void barrier(void) {asm volatile("" ::: "memory");}
static void stop_tx(void) {
    /* Disabling TX must retain NVM-loaded MULR/RRTHRESH and reserved bits.
     * SWXOFF is a command, so do not replay it during read/modify/write.
     */
    write_reg(TCTL,read_reg(TCTL)&~(TCTL_EN|(1u<<22)));
}
static int mine(void) {return active && current_task && current_task->id==owner;}
static int mmio_quiet(void) {
    if (reset_quiet && (int)(timer_ticks-mmio_quiet_until)>=0)reset_quiet=0;
    return reset_quiet;
}

/* Reap only completed, submitted descriptors. One ring slot is always free,
 * so clean==cur unambiguously means no outstanding frames. Runs with IF=0.
 */
static void reap_tx(void) {
    while (tx_clean!=tx_cur && (tx[tx_clean].status&1)) {
        tx_done++;tx_clean=(tx_clean+1)%TX_N;
    }
}

static void net_irq(PISR_FRAME frame) {
    (void)frame;
    DWORD cause=mmio_ready && !mmio_quiet() ? read_reg(ICR) : 0;
    if (cause) {irq_total++;info.last_icr=cause;}
    if (active && (cause&4))irq_test=1;
    if (active && (cause&1)) {reap_tx();task_wake_locked(owner,TASK_WAIT_NET);} /* TXDW */
    if (active && (cause&RX_IRQS)) {
        net_irq_count++;
        for (unsigned processed=0;processed<N;processed++) {
            DWORD status=rx[rx_cur].status;
            if (!(status&1))break;
            barrier();
            DWORD len=rx[rx_cur].info&0xffff,next=(head+1)%N;
            if ((status&3)==3 && !(status&0xff00) && len>=14 && len<=LW_NET_FRAME_MAX &&
                rx_buffers[rx_cur][12]==0x88 && rx_buffers[rx_cur][13]==0xb5 && next!=tail) {
                memcpy(queue[head].data,rx_buffers[rx_cur],len);
                queue[head].length=len;head=next;net_rx_count++;
                task_wake_locked(owner,TASK_WAIT_NET);
            } else net_dropped++;
            rx[rx_cur].status=0;
            barrier();write_reg(0x2818,rx_cur);
            rx_cur=(rx_cur+1)%N;
        }
    }
    if (irq>=8)lw_outb(0xa0,0x20);
    lw_outb(0x20,0x20);
}

static int find_device(void) {
    for (unsigned b=0;b<256;b++)for (unsigned d=0;d<32;d++) {
        DWORD id=lw_pci_read(b,d,0,0);
        if ((id&0xffff)==0xffff)continue;
        unsigned functions=(lw_pci_read(b,d,0,0x0c)&0x800000) ? 8 : 1;
        for (unsigned f=0;f<functions;f++) {
            DWORD pci_id=lw_pci_read(b,d,f,0);
            if (pci_id!=0x100e8086u && pci_id!=0x10f58086u)continue;
            info.pci_id=pci_id;info.bdf=(b<<16)|(d<<8)|f;
            DWORD address=lw_pci_read(b,d,f,0x10);
            DWORD line=lw_pci_read(b,d,f,0x3c);
            info.bar=address;info.irq=line&0xff;
            info.problem=LW_NET_BAD_BAR;
            if ((address&7) || !(address&~15u))continue;
            DWORD candidate=line&0xff;
            info.problem=LW_NET_BAD_IRQ;
            if (!((line>>8)&0xff))continue;
            if (candidate<3 || candidate>=12 || candidate==8)continue;
            ISR_HANDLER old=resman_isr_get_handler(0x20+candidate);
            info.problem=LW_NET_IRQ_BUSY;
            if (old && old!=net_irq)continue; /* No shared IRQ dispatcher yet. */
            bus=b;dev=d;fn=f;bar=address&~15u;irq=candidate;
            info.bar=bar;info.problem=LW_NET_OK;
            return 0;
        }
    }
    return RM_ERR_NOT_READY;
}

static DWORD pci_read(BYTE off) {
    DWORD flags=mem_irq_save(),v=lw_pci_read(bus,dev,fn,off);
    mem_irq_restore(flags);return v;
}
static void pci_write_word(BYTE off,WORD value) {
    DWORD flags=mem_irq_save();lw_pci_write_w(bus,dev,fn,off,value);mem_irq_restore(flags);
}
/* Restore D0 before touching MMIO. Walk the conventional capability list with
 * a strict bound; disable MSI/MSI-X so the PIC test really exercises INTx.
 */
static int power_up(void) {
    info.command=pci_read(4)&0xffff;info.pmcsr=0xffffffffu;
    if (pci_read(4)&(1u<<20)) {
        BYTE cap=pci_read(0x34)&0xfc;
        for (unsigned hops=0;cap && hops<48;hops++) {
            if (cap<0x40 || cap>0xfc)return RM_ERR_NOT_READY;
            DWORD header=pci_read(cap);BYTE kind=header&0xff;
            if (kind==1) {
                if (cap>0xf8)return RM_ERR_NOT_READY;
                WORD pm=pci_read(cap+4)&0xffff;
                if (pm&3) {
                    pci_write_word(cap+4,pm&~0x8003u); /* PME status is W1C. */
                    if (resman_sleep(10))return RM_ERR_NOT_READY;
                }
                info.pmcsr=pci_read(cap+4)&0xffff;
                if (info.pmcsr&3)return RM_ERR_NOT_READY;
            } else if (kind==5) pci_write_word(cap+2,(header>>16)&~1u);
            else if (kind==0x11) pci_write_word(cap+2,(header>>16)&~0x8000u);
            cap=(header>>8)&0xfc;
            if (cap && hops==47)return RM_ERR_NOT_READY;
        }
    }
    WORD command=pci_read(4)&0xffff;
    pci_write_word(4,(command|6)&~0x400u);
    info.command=pci_read(4)&0xffff;
    if ((info.command&0x406)!=6)return RM_ERR_NOT_READY;
    mmio_ready=1;return 0;
}
static void release_swflag(void) {
    if (swflag_owned) {write_reg(EXTCNF,read_reg(EXTCNF)&~SWFLAG);swflag_owned=0;}
}
static int acquire_swflag(void) {
    for (unsigned tries=0;tries<100;tries++) {
        DWORD flags=mem_irq_save();
        if (!(read_reg(EXTCNF)&SWFLAG)) {
            write_reg(EXTCNF,read_reg(EXTCNF)|SWFLAG);
            if (read_reg(EXTCNF)&SWFLAG) {
                swflag_owned=1;mem_irq_restore(flags);return 0;
            }
        }
        mem_irq_restore(flags);
        if (resman_sleep(1))break;
    }
    return RM_ERR_NOT_READY;
}
static int mdio(unsigned reg,WORD *value,int write) {
    /* Standard MII registers 0..15 are page-independent, PHY address 1.
     * READY=bit28, ERROR=bit30. No MDIO interrupts are enabled.
     */
    write_reg(MDIC,(reg<<16)|(1u<<21)|(write ? 1u<<26 : 2u<<26)|(write ? *value : 0));
    for (unsigned tries=0;tries<10;tries++) {
        DWORD reply=read_reg(MDIC);
        if (reply&(1u<<28)) {
            if ((reply&(1u<<30)) || ((reply>>16)&31)!=reg)return RM_ERR_NOT_READY;
            if (!write)*value=reply&0xffff;
            return 0;
        }
        if (resman_sleep(1))break;
    }
    return RM_ERR_NOT_READY;
}
static void claim_driver(void) {
    if (!driver_claimed) {
        old_drv_load=read_reg(CTRL_EXT)&DRV_LOAD;
        driver_claimed=1;
    }
    write_reg(CTRL_EXT,(read_reg(CTRL_EXT)|DRV_LOAD)&~(1u<<27));
}
static int reset_ich_mac(void) {
    /* Replacing ring pointers is not a reset of the on-chip descriptor/FIFO
     * state inherited from firmware. Reset the host data path before reusing
     * it (ICH SDM 10.4.1, 11.2/11.4; e1000e reset_hw_ich8lan).
     * Keep LCD_RST/PHY_RST clear: this is NOT a PHY reset or flash update.
     */
    info.problem=LW_NET_FW_BUSY;
    if (acquire_swflag())return RM_ERR_NOT_READY;
    claim_driver();
    info.problem=LW_NET_DMA_QUIESCE;
    write_reg(CTRL,(read_reg(CTRL)&~(1u<<31))|4); /* Master Disable */
    unsigned tries;
    for (tries=0;tries<100;tries++) {
        if (!(read_reg(STATUS)&(1u<<19)))break;
        if (resman_sleep(1))break;
    }
    if (read_reg(STATUS)&(1u<<19)) {
        write_reg(CTRL,read_reg(CTRL)&~4u);release_swflag();
        return RM_ERR_NOT_READY;
    }
    info.problem=LW_NET_RESET;
    DWORD flags=mem_irq_save();
    mmio_quiet_until=timer_ticks+21;reset_quiet=1;
    write_reg(CTRL,(read_reg(CTRL)&~(1u<<31))|(1u<<26));
    /* ICH must NOT be read/flushed immediately after reset; e1000e waits
     * 20ms. The reset clears SWFLAG itself, so do not write it back here.
     */
    swflag_owned=0;
    mem_irq_restore(flags);
    if (resman_sleep(20))return RM_ERR_NOT_READY;
    if (mmio_quiet())return RM_ERR_NOT_READY;
    for (tries=0;tries<100;tries++) {
        if (!(read_reg(CTRL)&(1u<<26)))break;
        if (resman_sleep(1))return RM_ERR_NOT_READY;
    }
    if (tries==100)return RM_ERR_NOT_READY;
    info.problem=LW_NET_NVM_LOAD;
    for (tries=0;tries<100;tries++) {
        if (read_reg(0x10)&(1u<<9))break; /* EEC.Auto_RD */
        if (resman_sleep(1))return RM_ERR_NOT_READY;
    }
    if (tries==100)return RM_ERR_NOT_READY;
    write_reg(IMC,0xffffffffu);(void)read_reg(ICR);
    write_reg(CTRL,read_reg(CTRL)&~4u); /* Permit DMA after the reset. */
    claim_driver(); /* NVM auto-load may have restored CTRL_EXT. */
    return 0;
}
static int prepare_82567(void) {
    /* MAC reset and NVM auto-load have completed. Preserve the PHY's link
     * and calibration: do not issue PHY_RST, MII reset, or EERD on ICH9.
     */
    info.problem=LW_NET_FW_BUSY;
    if (acquire_swflag())return RM_ERR_NOT_READY;
    DWORD flags=mem_irq_save();
    claim_driver();
    mem_irq_restore(flags);
    info.problem=LW_NET_MDIO;
    WORD bmcr=0;
    int rc=mdio(0,&bmcr,0);
    if (!rc && bmcr!=0xffffu) {
        info.bmcr=bmcr;
        /* If already linked, retain the negotiated configuration. Otherwise
         * clear PHY power-down/isolate and start auto-negotiation without reset.
         */
        if (!(read_reg(STATUS)&2)) {
            write_reg(PHY_CTRL,read_reg(PHY_CTRL)&~0x46u); /* LPLU D0/D3, GbE disable. */
            bmcr=(bmcr&~0x8c00u)|0x1200u; /* Never re-assert PHY reset. */
            rc=mdio(0,&bmcr,1);
            if (!rc)info.bmcr=bmcr;
        }
    } else rc=RM_ERR_NOT_READY;
    release_swflag();
    if (rc)return rc;
    /* Clear stale forced speed/duplex; use link indications from the PHY. */
    write_reg(CTRL,(read_reg(CTRL)|0x40)&~((1u<<11)|(1u<<12)));
    return 0;
}

static void setup_ich_tx(void) {
    /* ICH9 required initialization, also applied by e1000e ich8lan.c.
     * Receiving frames does not prove that its transmit arbiter is configured.
     * Program both TX queues' counting/writeback policy, with WTHRESH=1 so a
     * lone stop-and-wait ACK is not held waiting for a descriptor batch.
     */
    write_reg(CTRL_EXT,read_reg(CTRL_EXT)|(1u<<22));
    for (unsigned q=0;q<2;q++) {
        DWORD off=0x3828+q*0x100;
        DWORD control=read_reg(off)&~0x003f3f3fu;
        control|=(1u<<22)|(1u<<24)|(1u<<16)|(1u<<8)|0x1fu;
        write_reg(off,control);
    }
    write_reg(0x3840,read_reg(0x3840)|(1u<<23)|(1u<<24)|(1u<<26)|(1u<<27));
    /* e1000e ICH workaround: TARC1 bit28 is the inverse of TCTL.MULR. */
    DWORD tarc1=(read_reg(0x3940)&~(1u<<28))|(1u<<24)|(1u<<26)|(1u<<30);
    if (!(read_reg(TCTL)&TCTL_MULR))tarc1|=1u<<28;
    write_reg(0x3940,tarc1);
    /* Leave the unused queue empty. */
    write_reg(0x3908,0);write_reg(0x3910,0);write_reg(0x3918,0);
    write_reg(0x3820,0);write_reg(0x382c,0); /* No delayed TX interrupts. */
}

int resman_net_info(LW_NET_INFO *out) {
    if (!out)return RM_ERR_ARGUMENT;
    DWORD flags=mem_irq_save();
    if (mmio_ready && !mmio_quiet()) {
        info.ctrl=read_reg(CTRL);info.status=read_reg(STATUS);info.ctrl_ext=read_reg(CTRL_EXT);
        info.ral=read_reg(0x5400);info.rah=read_reg(0x5404);
        if (info.pci_id==0x10f58086u) {
            info.fwsm=read_reg(FWSM);info.extcnf=read_reg(EXTCNF);info.phy_ctrl=read_reg(PHY_CTRL);
        }
        info.rx_pending=active ? rx[rx_cur].status : 0;
        if (active)reap_tx();
        info.tdh=read_reg(0x3810);info.tdt=read_reg(0x3818);
        info.tctl=read_reg(TCTL);info.txdctl=read_reg(0x3828);info.tarc0=read_reg(0x3840);
        info.tarc1=read_reg(0x3940);
        info.tx_desc=active ? tx[(tx_cur+TX_N-1)%TX_N].status : 0;
        info.tdbal=read_reg(0x3800);info.tdbah=read_reg(0x3804);
        info.tdlen=read_reg(0x3808);info.tx_ring=(DWORD)tx;
        info.eec=read_reg(0x10);info.pba=read_reg(0x1000);info.pbs=read_reg(0x1008);
        info.tx0_addr=active ? tx[0].addr : 0;
        info.tx0_info=active ? tx[0].info : 0;
        info.tx0_status=active ? tx[0].status : 0;
    }
    info.tx_posted=tx_posted;info.tx_done=tx_done;info.tx_busy=tx_busy;
    info.mmio_ready=mmio_ready && !mmio_quiet();info.irq_count=irq_total;
    info.irq_test=irq_test;info.rx_count=net_rx_count;info.dropped=net_dropped;
    *out=info;mem_irq_restore(flags);return 0;
}

int resman_net_open(PBYTE mac) {
    if (!mac)return RM_ERR_ARGUMENT;
    DWORD flags=mem_irq_save();int rc=RM_ERR_NOT_READY;
    if (!(flags&0x200) || !current_task)goto done;
    if (active || opening)goto done;
    memzero(&info,sizeof(info));info.problem=LW_NET_NO_DEVICE;
    mmio_ready=irq_installed=swflag_owned=driver_claimed=0;irq_test=0;
    info.bmcr=0xffffffffu;
    rc=find_device();if (rc)goto done;
    owner=current_task->id;opening=1;
    mem_irq_restore(flags);
    info.problem=LW_NET_POWER;
    rc=power_up();if (rc)goto failed;
    write_reg(IMC,0xffffffffu);
    write_reg(RCTL,0);stop_tx();(void)read_reg(STATUS);
    if (resman_sleep(10)) {rc=RM_ERR_NOT_READY;goto failed;}
    if (info.pci_id==0x10f58086u) {
        rc=reset_ich_mac();if (rc)goto failed;
        rc=prepare_82567();if (rc)goto failed;
    } else {
        info.problem=LW_NET_RESET;
        write_reg(CTRL,read_reg(CTRL)|(1u<<26));
        unsigned tries;
        for (tries=0;tries<100000;tries++)if (!(read_reg(CTRL)&(1u<<26)))break;
        if (tries==100000) {rc=RM_ERR_NOT_READY;goto failed;}
    }
    write_reg(IMC,0xffffffffu);(void)read_reg(ICR);
    info.tctl_reset=read_reg(TCTL);
    write_reg(CTRL,read_reg(CTRL)|0x40); /* SLU. Preserve PHY/link settings. */
    info.problem=LW_NET_BAD_MAC;
    DWORD low=read_reg(0x5400),high=read_reg(0x5404);
    if (!(high&0x80000000u) || (low&1) || !(low|(high&0xffffu)) ||
        (low==0xffffffffu && (high&0xffffu)==0xffffu)) {rc=RM_ERR_NOT_READY;goto failed;}
    for (unsigned i=0;i<6;i++)mac[i]=(i<4 ? low>>(8*i) : high>>(8*(i-4)))&0xff;
    flags=mem_irq_save();
    write_reg(RCTL,0);stop_tx();
    for (unsigned i=0;i<N;i++)
        rx[i]=(DESC){(DWORD)rx_buffers[i],0,0,0};
    for (unsigned i=0;i<TX_N;i++) {
        tx[i]=(DESC){(DWORD)tx_buffers[i],0,0,1};
    }
    head=tail=rx_cur=tx_cur=tx_clean=0;waiting=0;
    tx_posted=tx_done=tx_busy=0;
    barrier();
    write_reg(0x2800,(DWORD)rx);write_reg(0x2804,0);write_reg(0x2808,sizeof(rx));
    write_reg(0x2810,0);write_reg(0x2818,N-1);
    write_reg(0x2820,0); /* RDTR=0: each completed packet can interrupt. */
    write_reg(0x282c,0);write_reg(0x00c4,0);
    if (info.pci_id==0x10f58086u) {
        /* Firmware may have used extended descriptors or interrupt auto-mask.
         * Select legacy RX writeback and explicit interrupt acknowledgement.
         */
        write_reg(0x5008,(read_reg(0x5008)&~0x8000u)|0xc0u);
        write_reg(0x00e0,0);
        setup_ich_tx();
    }
    write_reg(0x3800,(DWORD)tx);write_reg(0x3804,0);write_reg(0x3808,sizeof(tx));
    write_reg(0x3810,0);write_reg(0x3818,0);
    write_reg(0x0410,0x0060200a); /* copper TIPG */
    owner=current_task->id;active=1;
    resman_isr_set_handler(0x20+irq,net_irq);
    irq_installed=1;
    WORD elcr=0x4d0+(irq>>3);BYTE bit=1u<<(irq&7);
    elcr_saved=lw_inb(elcr)&bit;
    lw_outb(elcr,lw_inb(elcr)|bit); /* PCI INTx is level-triggered. */
    pic_irq_set(irq,1);
    /* Preserve NVM-loaded read-request policy, as e1000e/iPXE do. Writing
     * a fresh legacy constant here silently erased ICH's high control bits.
     */
    DWORD tctl=read_reg(TCTL)&~((0xffu<<4)|(0x3ffu<<12)|(1u<<22));
    write_reg(TCTL,tctl|TCTL_EN|8|(0x0fu<<4)|(0x3fu<<12)|(1u<<24));
    /* EN, broadcast accept, strip CRC; no promiscuous receive. */
    write_reg(RCTL,2|(1u<<15)|(1u<<26));
    (void)read_reg(ICR);write_reg(IMS,RX_IRQS|5);(void)read_reg(STATUS);
    /* Exercise the actual INTx route. Never consume ICR from the waiter. */
    info.problem=LW_NET_IRQ_TEST;irq_test=0;write_reg(0x00c8,4);
    mem_irq_restore(flags);
    for (unsigned i=0;i<20 && !irq_test;i++)resman_sleep(1);
    flags=mem_irq_save();
    if (!irq_test) {rc=RM_ERR_NOT_READY;shutdown();}
    else {info.problem=LW_NET_OK;opening=0;rc=0;}
done:
    mem_irq_restore(flags);return rc;
failed:
    flags=mem_irq_save();shutdown();mem_irq_restore(flags);return rc;
}

int resman_net_recv(PVOID buffer,DWORD capacity,DWORD timeout) {
    if (!buffer || capacity<LW_NET_FRAME_MAX || !timeout || timeout>60000)return RM_ERR_ARGUMENT;
    DWORD flags=mem_irq_save();int result=RM_ERR_NOT_READY;
    if (!(flags&0x200) || !mine())goto done;
    deadline=timer_ticks+timeout+1;waiting=1;
    while (head==tail && (int)(timer_ticks-deadline)<0)
        task_block_locked(TASK_WAIT_NET);
    waiting=0;
    result=0;
    if (head!=tail) {
        result=queue[tail].length;
        memcpy(buffer,queue[tail].data,result);tail=(tail+1)%N;
    }
done:
    mem_irq_restore(flags);return result;
}
int resman_net_timer_tick(DWORD now) {
    if (shutdown_pending && !mmio_quiet()) {shutdown();return 1;}
    if (active && waiting && (int)(now-deadline)>=0) {
        task_wake_locked(owner,TASK_WAIT_NET);waiting=0;return 1;
    }
    return 0;
}
int resman_net_send(PCVOID buffer,DWORD length) {
    if (!buffer || length<14 || length>LW_NET_FRAME_MAX)return RM_ERR_ARGUMENT;
    DWORD flags=mem_irq_save();int rc=RM_ERR_NOT_READY;
    if (!(flags&0x200) || !mine())goto done;
    reap_tx();
    if (!(tx[tx_cur].status&1)) {tx_busy++;goto done;}
    /* Keep one descriptor unused so TDT can never catch TDH with a full ring. */
    if ((tx_cur+1)%TX_N==read_reg(0x3810)) {tx_busy++;goto done;}
    memcpy(tx_buffers[tx_cur],buffer,length);
    if (length<60) {memzero(tx_buffers[tx_cur]+length,60-length);length=60;}
    tx[tx_cur].status=0;tx[tx_cur].info=length|(0x0bu<<24); /* EOP, IFCS, RS. */
    barrier();tx_cur=(tx_cur+1)%TX_N;write_reg(0x3818,tx_cur);
    tx_posted++;rc=0;
done:
    mem_irq_restore(flags);return rc;
}
static void shutdown(void) {
    /* If the opening task is killed in ICH's mandatory reset delay, even
     * cleanup must not touch MMIO yet. Keep the endpoint reserved; IRQ0
     * completes cleanup after the silence interval without reviving the task.
     */
    if (mmio_ready && mmio_quiet()) {
        shutdown_pending=1;owner=0xffffffffu;waiting=0;return;
    }
    if (mmio_ready) {
        release_swflag();
        write_reg(IMC,0xffffffffu);(void)read_reg(ICR);
        write_reg(RCTL,0);stop_tx();(void)read_reg(STATUS);
        if (driver_claimed) {
            write_reg(CTRL_EXT,(read_reg(CTRL_EXT)&~DRV_LOAD)|old_drv_load);
            driver_claimed=0;
        }
    }
    if (irq_installed) {
        pic_irq_set(irq,0);
        WORD port=0x4d0+(irq>>3);BYTE bit=1u<<(irq&7);
        lw_outb(port,(lw_inb(port)&~bit)|elcr_saved);
    }
    active=waiting=opening=irq_installed=shutdown_pending=0;owner=0xffffffffu;head=tail=0;
}
int resman_net_close(void) {
    DWORD flags=mem_irq_save();int rc=RM_ERR_NOT_READY;
    if (mine() && (flags&0x200)) {
        /* Let a final ACK/NAK leave before disabling TX. Completion and the
         * bounded deadline both wake this task; no descriptor busy polling.
         */
        deadline=timer_ticks+101;waiting=1;
        while (!(tx[(tx_cur+TX_N-1)%TX_N].status&1) && (int)(timer_ticks-deadline)<0)
            task_block_locked(TASK_WAIT_NET);
        rc=(tx[(tx_cur+TX_N-1)%TX_N].status&1) ? 0 : RM_ERR_TIMEOUT;
        shutdown();
    }
    mem_irq_restore(flags);return rc;
}
void resman_net_task_gone(DWORD id) {
    DWORD flags=mem_irq_save();
    if ((active || opening) && owner==id)shutdown();
    mem_irq_restore(flags);
}
