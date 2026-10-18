#include "stdint.h"
#include "io.h"
#include "pci.h"

static DWORD cfg_addr(BYTE bus, BYTE dev, BYTE func, BYTE off) {
    return 0x80000000u 
            | ((DWORD)bus<<16)
            | ((DWORD)(dev&0x1f)<<11)
            | ((DWORD)(func&7)<<8)
            | (off&0xfc);
}

DWORD pci_read_dword(BYTE bus, BYTE dev, BYTE func, BYTE off) {
    outl(PCI_CFG_ADDR, cfg_addr(bus, dev, func, off));
    return inl(PCI_CFG_DATA);
}
WORD pci_read_word(BYTE bus, BYTE dev, BYTE func, BYTE off) {
    DWORD v = pci_read_dword(bus, dev, func, off);
    return (WORD)(v >> ((off&2)<<3));
}
BYTE pci_read_byte(BYTE bus, BYTE dev, BYTE func, BYTE off) {
    DWORD v = pci_read_dword(bus, dev, func, off);
    return (BYTE)(v >> ((off&3)<<3));
}

void pci_write_dword(BYTE bus, BYTE dev, BYTE func, BYTE off, DWORD value) {
    outl(PCI_CFG_ADDR, cfg_addr(bus, dev, func, off));
    outl(PCI_CFG_DATA, value);
}
void pci_write_word(BYTE bus, BYTE dev, BYTE func, BYTE off, WORD value) {
    outl(PCI_CFG_ADDR, cfg_addr(bus, dev, func, off));
    outw(PCI_CFG_DATA + (off&2), value);
}

int pci_present(void) {
    DWORD saved, probe;

    saved=inl(PCI_CFG_ADDR);
    outl(PCI_CFG_ADDR, 0x80000000u);
    probe=inl(PCI_CFG_ADDR);
    outl(PCI_CFG_ADDR, saved);
    if (probe != 0x80000000u)return 0;
    return pci_read_word(0,0,0,PCI_VENDOR_ID)!=0xffff;
}