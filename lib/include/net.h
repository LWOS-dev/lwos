#ifndef _LW_NET_H
#define _LW_NET_H
#include "resman.h"
#define LW_NET_FRAME_MAX 1514u
enum {
    LW_NET_OK, LW_NET_NO_DEVICE, LW_NET_BAD_BAR, LW_NET_POWER,
    LW_NET_BAD_IRQ, LW_NET_IRQ_BUSY, LW_NET_FW_BUSY, LW_NET_MDIO,
    LW_NET_BAD_MAC, LW_NET_RESET, LW_NET_IRQ_TEST, LW_NET_BUSY,
    LW_NET_DMA_QUIESCE, LW_NET_NVM_LOAD
};
typedef struct {
    DWORD pci_id,bdf,bar,irq,command,pmcsr,problem;
    DWORD ctrl,status,ctrl_ext,fwsm,extcnf,phy_ctrl,ral,rah,bmcr;
    DWORD irq_count,rx_count,dropped,last_icr,irq_test,rx_pending;
    DWORD mmio_ready; /* 0: register/link fields are unavailable, not link-down. */
    DWORD tx_posted,tx_done,tx_busy,tdh,tdt,tx_desc,tctl,txdctl,tarc0;
    DWORD tdbal,tdbah,tdlen,tx_ring;
    DWORD eec,pba,pbs,tx0_addr,tx0_info,tx0_status;
    DWORD tctl_reset,tarc1;
} LW_NET_INFO;
/* Exclusive raw Ethernet endpoint, owned by the calling task. 82540EM and
 * 82567LM MAC reset/NVM auto-load with PHY configuration retained, PIC/INTx.
 * open copies the MAC; recv blocks atomically until IRQ data or timeout,
 * returning frame length, 0 on timeout, or an RM_* error. timeout: 1..60000 ms.
 * send copies the frame into a DMA ring, returns 0 when queued (NOT_READY if
 * full); close and task death disable RX/TX and release ownership. IF must be 1.
 */
#define rm_net_open RM_CALL(LW_RM_SLOT_NET_OPEN, int (*)(PBYTE))
#define rm_net_recv RM_CALL(LW_RM_SLOT_NET_RECV, int (*)(PVOID,DWORD,DWORD))
#define rm_net_send RM_CALL(LW_RM_SLOT_NET_SEND, int (*)(PCVOID,DWORD))
#define rm_net_close RM_CALL(LW_RM_SLOT_NET_CLOSE, int (*)(void))
/* Non-destructive diagnostic snapshot, also available after open fails. */
#define rm_net_info RM_CALL(LW_RM_SLOT_NET_INFO, int (*)(LW_NET_INFO *))
#endif
