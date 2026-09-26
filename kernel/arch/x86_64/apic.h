#ifndef ARCH_APIC_H
#define ARCH_APIC_H
#include "types.h"

/* ACPI (MADT) discovery */
struct acpi_info {
	paddr_t lapic_phys;
	u32 lapic_ids[16];
	u32 cpu_count;
	struct {
		u32 id;
		paddr_t phys;
		u32 gsi_base;
	} ioapics[4];
	u32 ioapic_count;
	/* ISA IRQ -> GSI overrides */
	u32 isa_gsi[16];
	u16 isa_flags[16];
	paddr_t mcfg_base;	/* PCIe ECAM, 0 if absent */
	char oem[7];
};
extern struct acpi_info acpi;
void acpi_init(void);
void acpi_poweroff(void);		/* returns only on failure */
bool acpi_can_poweroff(void);
u16 acpi_pm_timer_port(bool *is32);
NORETURN void machine_reboot(void);

void lapic_init(void);
void lapic_eoi(void);
u32 lapic_id(void);
void lapic_timer_start(u32 hz);
u64 lapic_timer_hz(void);
void ioapic_init(void);
void ioapic_route_isa(u8 isa_irq, u8 vector);
void ioapic_mask_isa(u8 isa_irq, bool masked);
void pic_disable(void);
/* MSI message address/data for vector on the boot CPU. */
u64 msi_address(void);
u32 msi_data(u8 vector);

void time_init(void);	/* calibrate TSC + LAPIC timer via the PIT */
void timer_irq_setup(u32 hz);
#endif
