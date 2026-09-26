/* Minimal ACPI table discovery: RSDP -> XSDT/RSDT -> MADT (+ MCFG, FADT).
 * No AML interpreter: poweroff decodes only the \_S5_ package from the DSDT
 * bytes (the usual minimal approach), reset uses the FADT reset register. */
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"

static struct {
	u32 smi_cmd, pm1a_cnt, pm1b_cnt;
	u8 acpi_enable;
	u16 slp_typa, slp_typb;
	bool s5_ok;
	bool reset_ok;
	u8 reset_space, reset_value;
	u64 reset_addr;
} pm;

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

/* Find "\_S5_" = Package { SLP_TYPa, SLP_TYPb, ... } in the DSDT AML. */
static void parse_s5(struct sdt *dsdt)
{
	u8 *p = (u8 *)dsdt + sizeof(struct sdt), *end = (u8 *)dsdt + dsdt->length;
	for (; p + 12 < end; p++) {
		if (memcmp(p, "_S5_", 4) || p[4] != 0x12)
			continue;
		if (!(p[-1] == 0x08 || (p[-2] == 0x08 && p[-1] == '\\')))
			continue;
		u8 *q = p + 5;
		q += ((*q & 0xC0) >> 6) + 2; /* PkgLength bytes + NumElements */
		if (*q == 0x0A)
			q++;
		pm.slp_typa = (u16)(*q++ << 10);
		if (*q == 0x0A)
			q++;
		pm.slp_typb = (u16)(*q << 10);
		pm.s5_ok = true;
		return;
	}
}

static void parse_fadt(struct sdt *t)
{
	u8 *f = (u8 *)t;
	pm.smi_cmd = *(u32 *)(f + 48);
	pm.acpi_enable = f[52];
	pm.pm1a_cnt = *(u32 *)(f + 64);
	pm.pm1b_cnt = *(u32 *)(f + 68);
	if (t->length >= 129 && (*(u32 *)(f + 112) & (1u << 10))) { /* RESET_REG_SUP */
		pm.reset_space = f[116];
		pm.reset_addr = *(u64 *)(f + 120);
		pm.reset_value = f[128];
		pm.reset_ok = pm.reset_addr != 0;
	}
	paddr_t dsdt = *(u32 *)(f + 40);
	if (t->length >= 148 && *(u64 *)(f + 140))
		dsdt = *(u64 *)(f + 140);
	if (dsdt)
		parse_s5(map_table(dsdt));
}

/* ACPI S5 (soft off). Returns only if it did not work. */
void acpi_poweroff(void)
{
	if (!pm.s5_ok || !pm.pm1a_cnt)
		return;
	if (!(inw((u16)pm.pm1a_cnt) & 1) && pm.smi_cmd && pm.acpi_enable) { /* SCI_EN clear */
		outb((u16)pm.smi_cmd, pm.acpi_enable);
		for (int i = 0; i < 300 && !(inw((u16)pm.pm1a_cnt) & 1); i++)
			delay_us(1000);
	}
	outw((u16)pm.pm1a_cnt, pm.slp_typa | (1 << 13));
	if (pm.pm1b_cnt)
		outw((u16)pm.pm1b_cnt, pm.slp_typb | (1 << 13));
	delay_us(100000);
}

/* Reset: FADT reset register, then the PCI reset port, then the 8042. */
NORETURN void machine_reboot(void)
{
	cli();
	if (pm.reset_ok) {
		if (pm.reset_space == 1)
			outb((u16)pm.reset_addr, pm.reset_value);
		else if (pm.reset_space == 0)
			*(volatile u8 *)acpi_map(pm.reset_addr, 1) = pm.reset_value;
		delay_us(50000);
	}
	outb(0xCF9, 0x02);
	delay_us(10);
	outb(0xCF9, 0x06);
	delay_us(50000);
	for (int i = 0; i < 1000 && (inb(0x64) & 2); i++)
		delay_us(100);
	outb(0x64, 0xFE);
	delay_us(50000);
	/* last resort: triple fault */
	struct PACKED { u16 limit; u64 base; } null_idt = { 0, 0 };
	__asm__ volatile("lidt %0; int3" ::"m"(null_idt));
	for (;;)
		hlt();
}

bool acpi_can_poweroff(void) { return pm.s5_ok; }

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
		else if (!memcmp(t->sig, "FACP", 4) && t->length >= 116)
			parse_fadt(t);
	}
	if (!acpi.lapic_phys || !acpi.ioapic_count)
		panic("acpi: MADT missing LAPIC or IOAPIC");
	KLOG("acpi", "OEM '%s', %u CPU(s), LAPIC %p, %u IOAPIC(s) (first %p gsi %u), ECAM %p",
	     acpi.oem, acpi.cpu_count, (void *)acpi.lapic_phys, acpi.ioapic_count,
	     (void *)acpi.ioapics[0].phys, acpi.ioapics[0].gsi_base, (void *)acpi.mcfg_base);
	KLOG("acpi", "PM1a_CNT 0x%x, S5 %s (SLP_TYPa %u), reset register %s", pm.pm1a_cnt,
	     pm.s5_ok ? "found" : "NOT found", pm.slp_typa >> 10, pm.reset_ok ? "yes" : "no");
}
