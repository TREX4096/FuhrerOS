/* PCI enumeration (legacy 0xCF8/0xCFC configuration mechanism, which QEMU's
 * q35 machine also supports) and MSI-X programming. Drivers are matched by
 * vendor/device ID from a static table. */
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"
#include "pci.h"

#define MAX_PCI 64
static struct pci_dev devs[MAX_PCI];
static u32 ndevs;

static u32 addr(struct pci_dev *d, u8 off)
{
	return 0x80000000u | ((u32)d->bus << 16) | ((u32)d->dev << 11) | ((u32)d->fn << 8) | (off & 0xFC);
}
u32 pci_read32(struct pci_dev *d, u8 off)
{
	outl(0xCF8, addr(d, off));
	return inl(0xCFC);
}
u16 pci_read16(struct pci_dev *d, u8 off) { return (u16)(pci_read32(d, off) >> ((off & 2) * 8)); }
u8 pci_read8(struct pci_dev *d, u8 off) { return (u8)(pci_read32(d, off) >> ((off & 3) * 8)); }
void pci_write32(struct pci_dev *d, u8 off, u32 v)
{
	outl(0xCF8, addr(d, off));
	outl(0xCFC, v);
}
void pci_write16(struct pci_dev *d, u8 off, u16 v)
{
	u32 cur = pci_read32(d, off);
	int sh = (off & 2) * 8;
	pci_write32(d, off, (cur & ~(0xFFFFu << sh)) | ((u32)v << sh));
}
void pci_write8(struct pci_dev *d, u8 off, u8 v)
{
	u32 cur = pci_read32(d, off);
	int sh = (off & 3) * 8;
	pci_write32(d, off, (cur & ~(0xFFu << sh)) | ((u32)v << sh));
}

u8 pci_find_cap(struct pci_dev *d, u8 id, u8 start)
{
	if (!(pci_read16(d, 0x06) & 0x10))
		return 0;
	u8 p = start ? pci_read8(d, start + 1) : pci_read8(d, 0x34);
	for (int guard = 0; p && guard < 48; guard++) {
		if (pci_read8(d, p) == id)
			return p;
		p = pci_read8(d, p + 1);
	}
	return 0;
}

void pci_enable_bus_master(struct pci_dev *d)
{
	u16 cmd = pci_read16(d, 0x04);
	cmd |= 0x6;		/* memory space + bus master */
	cmd |= 1u << 10;	/* legacy INTx off: we use MSI-X */
	pci_write16(d, 0x04, cmd);
}

int pci_msix_set(struct pci_dev *d, u16 entry, u8 vector)
{
	u8 cap = pci_find_cap(d, 0x11, 0);
	if (!cap)
		return -E_NOSYS;
	u32 tbl = pci_read32(d, cap + 4);
	u8 bir = tbl & 7;
	u64 off = tbl & ~7u;
	static void *mapped[MAX_PCI][6];
	u32 idx = (u32)(d - devs);
	if (!mapped[idx][bir])
		mapped[idx][bir] = ioremap(d->bar[bir], d->bar_size[bir]);
	volatile u32 *e = (volatile u32 *)((u8 *)mapped[idx][bir] + off + entry * 16);
	u64 a = msi_address();
	e[0] = (u32)a;
	e[1] = (u32)(a >> 32);
	e[2] = msi_data(vector);
	e[3] = 0; /* unmasked */
	return 0;
}

void pci_msix_enable(struct pci_dev *d)
{
	u8 cap = pci_find_cap(d, 0x11, 0);
	if (cap)
		pci_write16(d, cap + 2, (u16)((pci_read16(d, cap + 2) | 0x8000) & ~0x4000));
}

struct pci_dev *pci_devices(u32 *count)
{
	*count = ndevs;
	return devs;
}

static void read_bars(struct pci_dev *d)
{
	/* Disable decoding while sizing so the probes cannot alias anything. */
	u16 cmd = pci_read16(d, 0x04);
	pci_write16(d, 0x04, cmd & (u16)~0x3);
	for (int i = 0; i < 6; i++) {
		u8 off = (u8)(0x10 + i * 4);
		u32 lo = pci_read32(d, off);
		if (lo & 1) {
			d->bar_io[i] = lo != 0;
			d->bar[i] = lo & ~3u;
			continue;
		}
		bool is64 = ((lo >> 1) & 3) == 2 && i < 5;
		pci_write32(d, off, 0xFFFFFFFF);
		u32 mlo = pci_read32(d, off);
		pci_write32(d, off, lo);
		u64 base = lo & ~0xFULL, mask = (u64)(mlo & ~0xFu) | 0xFFFFFFFF00000000ULL;
		if (is64) {
			u32 hi = pci_read32(d, off + 4);
			pci_write32(d, off + 4, 0xFFFFFFFF);
			u32 mhi = pci_read32(d, off + 4);
			pci_write32(d, off + 4, hi);
			base |= (u64)hi << 32;
			mask = ((u64)mhi << 32) | (mlo & ~0xFu);
		}
		if (mlo & ~0xFu) {
			d->bar[i] = base;
			d->bar_size[i] = ~mask + 1;
		}
		if (is64)
			i++;
	}
	pci_write16(d, 0x04, cmd);
}

/* Driver table (virtio devices use the modern, non-transitional IDs). */
int virtio_blk_probe(struct pci_dev *d) __attribute__((weak));
int virtio_net_probe(struct pci_dev *d) __attribute__((weak));
static const struct pci_driver drivers[] = {
	{ "virtio-blk", 0x1AF4, 0x1042, NULL },
	{ "virtio-net", 0x1AF4, 0x1041, NULL },
};

static const char *class_name(u8 c, u8 s)
{
	switch (c) {
	case 0x01: return s == 0x06 ? "SATA controller" : s == 0x08 ? "NVMe controller" : "storage";
	case 0x02: return "network";
	case 0x03: return "display";
	case 0x06: return s == 0x00 ? "host bridge" : s == 0x01 ? "ISA bridge" : "bridge";
	case 0x0C: return s == 0x03 ? "USB controller" : s == 0x05 ? "SMBus" : "serial bus";
	}
	return "device";
}

static void gen_pci(void *pb);
void procfs_register(const char *name, void (*gen)(void *p));

void pci_init(void)
{
	for (u32 bus = 0; bus < 256; bus++)
		for (u8 dev = 0; dev < 32; dev++)
			for (u8 fn = 0; fn < 8; fn++) {
				struct pci_dev tmp = { .bus = (u8)bus, .dev = dev, .fn = fn };
				u32 id = pci_read32(&tmp, 0);
				if ((id & 0xFFFF) == 0xFFFF) {
					if (fn == 0)
						break;
					continue;
				}
				if (ndevs >= MAX_PCI)
					break;
				struct pci_dev *d = &devs[ndevs++];
				*d = tmp;
				d->vendor = id & 0xFFFF;
				d->device = id >> 16;
				u32 cls = pci_read32(d, 0x08);
				d->class = cls >> 24;
				d->subclass = (cls >> 16) & 0xFF;
				d->prog_if = (cls >> 8) & 0xFF;
				d->irq_line = pci_read8(d, 0x3C);
				read_bars(d);
				if (fn == 0 && !(pci_read8(d, 0x0E) & 0x80))
					break; /* single-function device */
			}
	for (u32 i = 0; i < ndevs; i++) {
		struct pci_dev *d = &devs[i];
		KLOG("pci", "%02x:%02x.%u %04x:%04x %s", d->bus, d->dev, d->fn, d->vendor, d->device,
		     class_name(d->class, d->subclass));
		int (*probe)(struct pci_dev *) = NULL;
		if (d->vendor == 0x1AF4 && d->device == 0x1042)
			probe = virtio_blk_probe;
		else if (d->vendor == 0x1AF4 && d->device == 0x1041)
			probe = virtio_net_probe;
		if (probe) {
			int r = probe(d);
			d->driver = r == 0 ? (d->device == 0x1042 ? "virtio-blk" : "virtio-net") : NULL;
		}
	}
	(void)drivers;
	procfs_register("pci", (void (*)(void *))gen_pci);
}

struct pbuf;
void pb_printf(struct pbuf *p, const char *fmt, ...);
static void gen_pci(void *pb)
{
	for (u32 i = 0; i < ndevs; i++) {
		struct pci_dev *d = &devs[i];
		pb_printf(pb, "%02x:%02x.%u %04x:%04x %-18s %s\n", d->bus, d->dev, d->fn, d->vendor,
			  d->device, class_name(d->class, d->subclass), d->driver ? d->driver : "-");
	}
}
