/* Minimal ACPI table discovery: RSDP -> XSDT/RSDT -> MADT (+ MCFG).
 * No AML interpreter: only static tables are needed at this stage. */
#include "arch/x86_64/apic.h"
#include "kernel.h"
#include "mm.h"

struct acpi_info acpi;

struct PACKED rsdp {
	char sig[8];
	u8 checksum;
	char oem[6];
	u8 revision;
	u32 rsdt;
	u32 length;
	u64 xsdt;
	u8 xchecksum;
	u8 rsvd[3];
};

struct PACKED sdt {
	char sig[4];
	u32 length;
	u8 revision;
	u8 checksum;
	char oem[6];
	char oem_table[8];
	u32 oem_rev;
	u32 creator;
	u32 creator_rev;
};

static void *acpi_map(paddr_t p, u64 len)
{
	if (vmm_translate(vmm_kernel_space(), (vaddr_t)phys_to_virt(p)) &&
	    vmm_translate(vmm_kernel_space(), (vaddr_t)phys_to_virt(p + len - 1)))
		return phys_to_virt(p);
	return ioremap(p, len);
}

static bool checksum_ok(const void *p, u32 len)
{
	u8 s = 0;
	for (u32 i = 0; i < len; i++)
		s += ((const u8 *)p)[i];
	return s == 0;
}

static struct sdt *map_table(paddr_t p)
{
	struct sdt *h = acpi_map(p, sizeof(struct sdt));
	return acpi_map(p, h->length);
}

static void parse_madt(struct sdt *t)
{
	u8 *p = (u8 *)t + sizeof(struct sdt);
	acpi.lapic_phys = *(u32 *)p;
	u8 *end = (u8 *)t + t->length;
	p += 8; /* lapic address + flags */
	while (p + 2 <= end && p[1]) {
		switch (p[0]) {
		case 0: /* processor local APIC */
			if ((*(u32 *)(p + 4) & 1) && acpi.cpu_count < 16)
				acpi.lapic_ids[acpi.cpu_count++] = p[3];
			break;
		case 1: /* I/O APIC */
			if (acpi.ioapic_count < 4) {
				acpi.ioapics[acpi.ioapic_count].id = p[2];
				acpi.ioapics[acpi.ioapic_count].phys = *(u32 *)(p + 4);
				acpi.ioapics[acpi.ioapic_count].gsi_base = *(u32 *)(p + 8);
				acpi.ioapic_count++;
			}
			break;
		case 2: /* interrupt source override */
			if (p[3] < 16) {
				acpi.isa_gsi[p[3]] = *(u32 *)(p + 4);
				acpi.isa_flags[p[3]] = *(u16 *)(p + 8);
			}
			break;
		case 5: /* 64-bit LAPIC address override */
			acpi.lapic_phys = *(u64 *)(p + 4);
			break;
		}
		p += p[1];
	}
}

void acpi_init(void)
{
	for (int i = 0; i < 16; i++)
		acpi.isa_gsi[i] = (u32)i;
	if (!boot.rsdp)
		panic("acpi: no RSDP from the bootloader");
	struct rsdp *r = acpi_map(boot.rsdp, sizeof(*r));
	if (memcmp(r->sig, "RSD PTR ", 8) || !checksum_ok(r, 20))
		panic("acpi: bad RSDP");
	memcpy(acpi.oem, r->oem, 6);
	bool x = r->revision >= 2 && r->xsdt;
	struct sdt *root = map_table(x ? r->xsdt : r->rsdt);
	u32 n = (root->length - sizeof(struct sdt)) / (x ? 8 : 4);
	u8 *ents = (u8 *)root + sizeof(struct sdt);
	for (u32 i = 0; i < n; i++) {
		paddr_t p = x ? ((u64 *)ents)[i] : ((u32 *)ents)[i];
		struct sdt *t = map_table(p);
		if (!memcmp(t->sig, "APIC", 4))
			parse_madt(t);
		else if (!memcmp(t->sig, "MCFG", 4) && t->length >= 44 + 16)
			acpi.mcfg_base = *(u64 *)((u8 *)t + 44);
	}
	if (!acpi.lapic_phys || !acpi.ioapic_count)
		panic("acpi: MADT missing LAPIC or IOAPIC");
	KLOG("acpi", "OEM '%s', %u CPU(s), LAPIC %p, %u IOAPIC(s) (first %p gsi %u), ECAM %p",
	     acpi.oem, acpi.cpu_count, (void *)acpi.lapic_phys, acpi.ioapic_count,
	     (void *)acpi.ioapics[0].phys, acpi.ioapics[0].gsi_base, (void *)acpi.mcfg_base);
}
