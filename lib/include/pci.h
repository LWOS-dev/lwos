#ifndef _LW_PCI_H
#define _LW_PCI_H

#include "io.h"
#include "stdint.h"

#define PCI_CFG_ADDR   0xCF8
#define PCI_CFG_DATA   0xCFC

#define PCI_VENDOR_ID   0x00
#define PCI_DEVICE_ID   0x02
#define PCI_COMMAND     0x04
#define PCI_STATUS      0x06
#define PCI_REVISION    0x08
#define PCI_PROG_IF     0x09
#define PCI_SUBCLASS    0x0A
#define PCI_CLASS       0x0B
#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0        0x10
#define PCI_SUBSYS_VEN  0x2C
#define PCI_SUBSYS_ID   0x2E
#define PCI_CAP_PTR     0x34
#define PCI_IRQ_LINE    0x3C
#define PCI_IRQ_PIN     0x3D

#define PCI_HDR_MULTIFN 0x80    /* bit in the header-type byte */
#define PCI_HDR_TYPE    0x7F

DWORD pci_read_dword(BYTE bus, BYTE dev, BYTE func, BYTE off);
WORD pci_read_word(BYTE bus, BYTE dev, BYTE func, BYTE off);
BYTE pci_read_byte(BYTE bus, BYTE dev, BYTE func, BYTE off);

void pci_write_dword(BYTE bus, BYTE dev, BYTE func, BYTE off, DWORD value);
void pci_write_word(BYTE bus, BYTE dev, BYTE func, BYTE off, WORD value);

int pci_present(void);

#endif