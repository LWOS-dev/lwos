#include "dev/ahci.h"
#include "pci.h"

#define AHCI_CAP     0x00
#define AHCI_GHC     0x04
#define AHCI_IS      0x08
#define AHCI_PI      0x0C
#define AHCI_VS      0x10

#define AHCI_GHC_AE  0x80000000

#define AHCI_PORT_BASE(port)  (0x100u + 0x80u * (u32)(port))
#define AHCI_PxCLB   0x00
#define AHCI_PxCLBU  0x04
#define AHCI_PxFB    0x08
#define AHCI_PxFBU   0x0C
#define AHCI_PxIS    0x10
#define AHCI_PxIE    0x14
#define AHCI_PxCMD   0x18
#define AHCI_PxTFD   0x20
#define AHCI_PxSIG   0x24
#define AHCI_PxSSTS  0x28
#define AHCI_PxSCTL  0x2C
#define AHCI_PxSERR  0x30
#define AHCI_PxCI    0x38

#define AHCI_PxCMD_ST   (1u << 0)
#define AHCI_PxCMD_FRE  (1u << 4)
#define AHCI_PxCMD_FR   (1u << 14)
#define AHCI_PxCMD_CR   (1u << 15)

#define AHCI_SIG_ATAPI  0xEB140101u

#define AHCI_IS_TFES    (1u << 30)   /* PxIS: Task File Error Status */

#define AHCI_FIS_REG_H2D  0x27

#define AHCI_CMD_READ_DMA_EXT   0x25
#define AHCI_CMD_WRITE_DMA_EXT  0x35

#define AHCI_MAX_PORTS  ATA_MAX_DEVICES   /* 4: reuse the legacy cap, no growth */
