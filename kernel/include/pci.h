#ifndef FUHRER_PCI_H
#define FUHRER_PCI_H
#include "types.h"

struct pci_dev {
	u8 bus, dev, fn;
	u16 vendor, device;
	u8 class, subclass, prog_if;
	u8 irq_line;
	u64 bar[6];		/* physical base, 0 if unused */
	u64 bar_size[6];
	bool bar_io[6];
	const char *driver;
};

void pci_init(void);
u32 pci_read32(struct pci_dev *d, u8 off);
u16 pci_read16(struct pci_dev *d, u8 off);
u8 pci_read8(struct pci_dev *d, u8 off);
void pci_write32(struct pci_dev *d, u8 off, u32 v);
void pci_write16(struct pci_dev *d, u8 off, u16 v);
void pci_write8(struct pci_dev *d, u8 off, u8 v);
u8 pci_find_cap(struct pci_dev *d, u8 id, u8 start);
void pci_enable_bus_master(struct pci_dev *d);
/* Program MSI-X table entry `entry` to deliver `vector` to the BSP. */
int pci_msix_set(struct pci_dev *d, u16 entry, u8 vector);
void pci_msix_enable(struct pci_dev *d);
struct pci_dev *pci_devices(u32 *count);

struct pci_driver {
	const char *name;
	u16 vendor, device;	/* device 0xFFFF = any */
	int (*probe)(struct pci_dev *d);
};
#endif
