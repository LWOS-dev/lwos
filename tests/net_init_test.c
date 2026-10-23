/* Register-model tests for the un-emulated ICH9/82567 firmware handoff.
 * These verify register ordering/error cleanup, not physical link operation.
 */
#include <assert.h>
#include "mem.h"
static DWORD test_lock(void) {return 0x200;}
static void test_unlock(DWORD flags) {(void)flags;}
#define mem_irq_save test_lock
#define mem_irq_restore test_unlock
#define LW_NET_TEST 1
#include "../app/resman/src/net.c"

PVOID *lw_abi_base;
TASK task[TASK_MAX];
PTASK current_task=&task[1];
volatile DWORD timer_ticks;
static PVOID abi[LW_SLOT_COUNT];
static DWORD config[64],regs[0x6000/4],reset_writes,mdio_writes;
static WORD phy_bmcr;
static int fail_mdio,fail_probe,occupied,pic_enabled,reset_timeout,sticky_d3;
static int stall_tx;
static int master_busy,auto_load_timeout;
static DWORD reset_guard_until;
static DWORD reset_tctl;
static unsigned wire_count;
static BYTE last_wire[60];
static ISR_HANDLER handler;
PVOID memcpy(PVOID out,PCVOID in,int count) {
    for (int i=0;i<count;i++)((PBYTE)out)[i]=((PCBYTE)in)[i];
    return out;
}
PVOID memzero(PVOID out,int count) {
    for (int i=0;i<count;i++)((PBYTE)out)[i]=0;
    return out;
}
static DWORD read_pci(BYTE b,BYTE d,BYTE f,BYTE off) {
    return !b && d==25 && !f ? config[off/4] : 0xffffffffu;
}
static void write_pci_word(BYTE b,BYTE d,BYTE f,BYTE off,WORD value) {
    assert(!b && d==25 && !f);
    unsigned shift=(off&2)*8;DWORD mask=0xffffu<<shift;
    config[off/4]=(config[off/4]&~mask)|((DWORD)value<<shift);
    if (off==0x54 && sticky_d3)config[off/4]|=3;
}
static BYTE input_byte(WORD port) {(void)port;return 0;}
static void output_byte(WORD port,BYTE value) {(void)port;(void)value;}
ISR_HANDLER resman_isr_get_handler(DWORD vector) {
    (void)vector;return occupied ? (ISR_HANDLER)1 : handler;
}
int resman_isr_set_handler(DWORD vector,ISR_HANDLER callback) {
    assert(vector==0x2b);handler=callback;return 0;
}
void pic_irq_set(DWORD line,int enable) {assert(line==11);pic_enabled=enable;}
void task_wake_locked(DWORD id,DWORD reason) {assert(id==1 && reason==TASK_WAIT_NET);}
void task_block_locked(DWORD reason) {(void)reason;assert(!"unexpected block");}
int resman_sleep(DWORD ms) {
    if (reset_quiet) {
        LW_NET_INFO snapshot;
        assert(ms==20 && !resman_net_info(&snapshot) && !snapshot.mmio_ready);
        /* A diagnostic caller must not touch MMIO while the opener sleeps. */
    }
    timer_ticks+=ms+1;return 0;
}
DWORD net_test_read_reg(DWORD off) {
    assert((config[4/4]&6)==6 && !(config[0x54/4]&3));
    assert(off<sizeof(regs));DWORD value=regs[off/4];
    assert(!reset_guard_until || (int)(timer_ticks-reset_guard_until)>=0);
    if (off==ICR)regs[off/4]=0;
    return value;
}
void net_test_write_reg(DWORD off,DWORD value) {
    assert((config[4/4]&6)==6 && !(config[0x54/4]&3));
    assert(off<sizeof(regs));
    assert(!reset_guard_until || (int)(timer_ticks-reset_guard_until)>=0);
    if (off==CTRL && (value&(1u<<26))) {
        reset_writes++;
        if (info.pci_id==0x10f58086u) {
            /* ICH reset is issued with engines idle, DMA quiesced and the
             * semaphore held, without propagating a reset to the PHY.
             * Any MMIO access before the mandatory 20ms wait is a failure.
             */
            assert(!regs[RCTL/4] && !(regs[TCTL/4]&2) && swflag_owned);
            assert(!(regs[STATUS/4]&(1u<<19)) && !(value&(1u<<31)));
            reset_guard_until=timer_ticks+20;
            regs[EXTCNF/4]&=~SWFLAG;regs[CTRL_EXT/4]=0;
            regs[0x10/4]=auto_load_timeout ? 0 : 0x300; /* NVM present/auto-read done */
            for (unsigned i=0x3800/4;i<0x3a00/4;i++)regs[i]=0;
            regs[0x3840/4]=0x403;regs[0x3940/4]=0x401;
            /* ICH SDM 10.4.52: bit28 defaults to one, RRTHRESH[30:29]
             * defaults to 01. NVM auto-load supplies the high TX control bits.
             */
            regs[TCTL/4]=reset_tctl;
        }
        if (!reset_timeout)value&=~(1u<<26);
    }
    if (off==CTRL && (value&4) && !master_busy)regs[STATUS/4]&=~(1u<<19);
    if (off==MDIC) {
        if (fail_mdio) {
            regs[off/4]=fail_mdio==1 ? value|(1u<<28)|(1u<<30) : value;
            return;
        }
        assert((value>>21&31)==1 && (value>>16&31)==0);
        if (value&(1u<<26)) {phy_bmcr=value&0xffff;mdio_writes++;}
        regs[off/4]=(value&0xffff0000u)|phy_bmcr|(1u<<28);return;
    }
    if (off==IMC) {regs[IMS/4]&=~value;return;}
    if (off==IMS) {regs[IMS/4]|=value;return;}
    regs[off/4]=value;
    if (off==0x3818 && active && !stall_tx) {
        /* An ICH transmitter with the required arbiter/counting/writeback
         * setup can consume a single ACK; stale queue1 must be disabled.
         */
        /* Intel ICH SDM 10.4.58: TX rings require >=16 descriptors,
         * in multiples of 128 bytes. QEMU's 82540 model accepts eight.
         * Refuse the invalid size here rather than modelling QEMU's leniency.
         */
        DWORD length=regs[0x3808/4];
        int ready=length>=16u*16u && !(length&127u) &&
            (regs[CTRL_EXT/4]&(1u<<22)) &&
            (regs[0x3828/4]&0x017f3f3f)==0x0141011f &&
            (regs[0x3928/4]&0x017f3f3f)==0x0141011f &&
            (regs[0x3840/4]&0x0d800000)==0x0d800000 && !regs[0x3908/4];
        if (!ready)return;
        while (regs[0x3810/4]!=value) {
            DWORD slot=regs[0x3810/4];
            assert((tx[slot].info&0xffff)==60 && (tx[slot].info>>24)==0x0b);
            memcpy(last_wire,tx_buffers[slot],60);wire_count++;
            tx[slot].status=1;regs[0x3810/4]=(slot+1)%TX_N;
        }
        regs[ICR/4]|=1;handler(0);
    }
    if (off==0xc8 && !fail_probe) {
        assert(handler && pic_enabled && (regs[IMS/4]&4));
        regs[ICR/4]|=4;handler(0);
    }
}
static void reset_model(DWORD id) {
    memzero(config,sizeof(config));memzero(regs,sizeof(regs));
    config[0]=id;config[4/4]=(1u<<20)|0x400; /* PM cap present; INTx disabled. */
    config[0x10/4]=0xf0000000u;config[0x3c/4]=0x10b;
    config[0x34/4]=0x50;config[0x50/4]=0x6001; /* PM -> MSI. */
    config[0x54/4]=0x8103;config[0x60/4]=0x00010005; /* D3; MSI enabled. */
    regs[0x5400/4]=0x12005452;regs[0x5404/4]=0x80005634;
    regs[CTRL/4]=(1u<<11)|(1u<<12);regs[PHY_CTRL/4]=0x104e;
    regs[CTRL_EXT/4]=(1u<<27)|0x80;regs[0x5008/4]=0x8800;regs[0xe0/4]=0xffffffffu;
    regs[0x3908/4]=128;regs[0x3918/4]=4;
    phy_bmcr=0x0c00;reset_writes=mdio_writes=0;
    fail_mdio=fail_probe=occupied=reset_timeout=sticky_d3=0;handler=0;pic_enabled=0;
    task[1].id=1;timer_ticks=0;
    stall_tx=wire_count=0;
    master_busy=auto_load_timeout=0;reset_guard_until=0;
    reset_tctl=0x3003f0f8;
    assert(!active && !opening && !swflag_owned);
}
int main(void) {
    abi[LW_SLOT_PCI_READ_DWORD]=(PVOID)read_pci;
    abi[LW_SLOT_PCI_WRITE_WORD]=(PVOID)write_pci_word;
    abi[LW_SLOT_IO_INB]=(PVOID)input_byte;abi[LW_SLOT_IO_OUTB]=(PVOID)output_byte;
    lw_abi_base=abi;
    BYTE mac[6];LW_NET_INFO n;
    reset_model(0x10f58086u);
    assert(!resman_net_open(mac));
    assert(reset_writes==1 && mdio_writes==1 && phy_bmcr==0x1200);
    assert((regs[TCTL/4]&0x70000000)==(reset_tctl&0x70000000));
    assert(!(regs[EXTCNF/4]&SWFLAG) && regs[PHY_CTRL/4]==(0x104e&~0x46));
    assert(!(regs[CTRL/4]&((1u<<11)|(1u<<12))) && (regs[CTRL/4]&0x40));
    assert((regs[CTRL_EXT/4]&DRV_LOAD) && !(regs[CTRL_EXT/4]&(1u<<27)));
    assert(regs[0x5008/4]==0x8c0 && !regs[0xe0/4]);
    assert(!(regs[CTRL/4]&4) && !swflag_owned && timer_ticks>=30);
    BYTE ack[60];for (unsigned i=0;i<sizeof(ack);i++)ack[i]=(BYTE)i;
    assert(!resman_net_send(ack,sizeof(ack)) && wire_count==1);
    for (unsigned i=0;i<sizeof(ack);i++)assert(last_wire[i]==ack[i]);
    assert(!resman_net_info(&n) && n.tx_posted==1 && n.tx_done==1 && !n.tx_busy);
    assert(n.tctl_reset==reset_tctl && n.tctl==0x3103f0fa);
    assert(!(n.tarc1&(1u<<28))); /* MULR=1 -> TARC1 bit28=0. */
    assert(n.tdh==1 && n.tdt==1 && (n.tx_desc&1));
    assert(n.tdlen>=256 && n.tdlen==sizeof(tx) && !n.tdbah && n.tdbal==n.tx_ring);
    /* An undersized firmware ring stalls; restoring a legal size lets the
     * model consume the queued descriptor. Also exercise TX wrap separately
     * from the smaller RX ring and verify completion accounting across wraps.
     */
    regs[0x3808/4]=128;
    assert(!resman_net_send(ack,sizeof(ack)) && wire_count==1);
    regs[0x3808/4]=sizeof(tx);
    net_test_write_reg(0x3818,tx_cur);
    assert(wire_count==2);
    for (unsigned i=0;i<2*TX_N+3;i++)assert(!resman_net_send(ack,sizeof(ack)));
    assert(!resman_net_info(&n) && n.tx_posted==2*TX_N+5 && n.tx_done==n.tx_posted);
    assert(n.tdh==5 && n.tdt==5 && !n.tx_busy);
    assert((config[0x54/4]&0x8003)==0 && !(config[0x60/4]&(1u<<16)));
    assert(!resman_net_info(&n) && n.pci_id==0x10f58086u && n.problem==LW_NET_OK && n.irq_test);
    regs[ICR/4]=0x80;
    assert(!resman_net_info(&n) && regs[ICR/4]==0x80); /* Info must not acknowledge IRQs. */
    assert(mac[0]==0x52 && mac[5]==0x56 && !resman_net_close());
    assert(!(regs[TCTL/4]&TCTL_EN) && (regs[TCTL/4]&0x70000000)==0x30000000);
    assert(!(regs[CTRL_EXT/4]&DRV_LOAD) && !driver_claimed);
    /* Retain different NVM read-request policies, including MULR disabled.
     * The arbiter workaround must follow the actual bit, not assume a mode.
     */
    reset_model(0x10f58086u);reset_tctl=0x2003f0f8;
    assert(!resman_net_open(mac) && !resman_net_info(&n));
    assert((n.tctl&0x70000000)==0x20000000 && (n.tarc1&(1u<<28)));
    assert(!resman_net_close());
    reset_model(0x10f58086u);reset_tctl=0x5003f0f8;
    assert(!resman_net_open(mac) && !resman_net_info(&n));
    assert((n.tctl&0x70000000)==0x50000000 && !(n.tarc1&(1u<<28)));
    assert(!resman_net_close());
    reset_model(0x10f58086u);regs[STATUS/4]=2;
    assert(!resman_net_open(mac) && !mdio_writes && phy_bmcr==0x0c00);
    assert(!resman_net_close());
    reset_model(0x10f58086u);stall_tx=1;
    assert(!resman_net_open(mac));
    for (unsigned i=0;i<TX_N-1;i++)assert(!resman_net_send(ack,sizeof(ack)));
    assert(resman_net_send(ack,sizeof(ack))==RM_ERR_NOT_READY);
    assert(!resman_net_info(&n) && n.tx_posted==TX_N-1 && !n.tx_done && n.tx_busy==1);
    assert(!n.tdh && n.tdt==TX_N-1 && !(n.tx_desc&1));
    resman_net_task_gone(1);
    reset_model(0x10f58086u);regs[EXTCNF/4]=SWFLAG;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_FW_BUSY && regs[EXTCNF/4]==SWFLAG);
    assert(!active && !opening && !pic_enabled);
    reset_model(0x10f58086u);fail_mdio=1;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_MDIO && !(regs[EXTCNF/4]&SWFLAG));
    assert(!driver_claimed && !(regs[CTRL_EXT/4]&DRV_LOAD));
    reset_model(0x10f58086u);fail_mdio=2;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_MDIO && !swflag_owned);
    reset_model(0x10f58086u);fail_probe=1;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_IRQ_TEST && !pic_enabled);
    assert(!active && !opening && !regs[RCTL/4] && !(regs[TCTL/4]&2));
    reset_model(0x10f58086u);config[0x3c/4]=0x1ff;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_BAD_IRQ);
    reset_model(0x10f58086u);occupied=1;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_IRQ_BUSY);
    reset_model(0x10f58086u);sticky_d3=1;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_POWER && !mmio_ready);
    reset_model(0x10f58086u);config[0x50/4]=0x5001;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_POWER && !mmio_ready);
    reset_model(0x10f58086u);regs[0x5400/4]=0;regs[0x5404/4]=0;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_BAD_MAC);
    reset_model(0x10f58086u);phy_bmcr=0x8c00;
    assert(!resman_net_open(mac) && phy_bmcr==0x1200 && reset_writes==1);
    assert(!resman_net_close());
    reset_model(0x10f58086u);master_busy=1;regs[STATUS/4]=1u<<19;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_DMA_QUIESCE && !reset_writes);
    assert(!swflag_owned && !driver_claimed && !(regs[CTRL/4]&4) && !active && !opening);
    reset_model(0x10f58086u);reset_timeout=1;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_RESET && reset_writes==1);
    assert(!swflag_owned && !driver_claimed && !active && !opening);
    reset_model(0x10f58086u);auto_load_timeout=1;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_NVM_LOAD && reset_writes==1);
    assert(!swflag_owned && !driver_claimed && !active && !opening);
    reset_model(0x10f58086u);regs[CTRL_EXT/4]|=DRV_LOAD;
    assert(!resman_net_open(mac) && reset_writes==1);
    assert(!resman_net_close() && (regs[CTRL_EXT/4]&DRV_LOAD));
    reset_model(0x100e8086u);
    assert(!resman_net_open(mac) && reset_writes==1 && !mdio_writes);
    assert(!resman_net_close());
    reset_model(0x100e8086u);reset_timeout=1;
    assert(resman_net_open(mac)<0 && info.problem==LW_NET_RESET && !active && !opening);
    reset_model(0x10f58086u);
    assert(!resman_net_open(mac));resman_net_task_gone(1);
    assert(!active && !opening && !pic_enabled && !driver_claimed);
    assert(!resman_net_open(mac) && !resman_net_close());
    /* Model task death during reset: reserve ownership until IRQ0 can
     * safely shut down, and refuse diagnostics/new opens in the meantime.
     */
    reset_model(0x10f58086u);
    assert(!resman_net_open(mac));
    active=0;opening=1;reset_quiet=1;
    mmio_quiet_until=reset_guard_until=timer_ticks+21;
    resman_net_task_gone(1);
    assert(shutdown_pending && opening && driver_claimed);
    assert(!resman_net_info(&n) && !n.mmio_ready);
    assert(resman_net_open(mac)==RM_ERR_NOT_READY && shutdown_pending);
    timer_ticks+=20;
    assert(!resman_net_timer_tick(timer_ticks) && shutdown_pending);
    timer_ticks++;
    assert(resman_net_timer_tick(timer_ticks));
    assert(!shutdown_pending && !reset_quiet && !opening && !driver_claimed && !pic_enabled);
    assert(!resman_net_open(mac) && !resman_net_close());
    return 0;
}
