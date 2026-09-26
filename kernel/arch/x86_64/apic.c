/* Local APIC, I/O APIC, legacy PIC shutdown, PIT-based calibration and the
 * scheduler timer. Uniprocessor: everything targets the bootstrap CPU. */
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "kernel.h"
#include "mm.h"

#define LAPIC_ID 0x020
#define LAPIC_EOI 0x0B0
#define LAPIC_SVR 0x0F0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR 0x390
#define LAPIC_TIMER_DIV 0x3E0

static volatile u32 *lapic;
static volatile u32 *ioapic;
static u32 ioapic_gsi_base, ioapic_max;
static u64 lapic_ticks_per_sec;
static u64 tsc_hz, tsc_boot, ns_mult;	/* ns = (tsc * ns_mult) >> 32 */
static volatile u64 tick_count;
static u32 timer_hz;

/* (hi:lo) / d without libgcc (__udivti3): restoring shift-subtract. The
 * kernel links no libgcc, whose objects were built with the red zone. */
static u64 div128_64(u64 hi, u64 lo, u64 d)
{
	u64 q = 0, r = hi;
	for (int i = 63; i >= 0; i--) {
		bool carry = r >> 63;
		r = (r << 1) | ((lo >> i) & 1);
		q <<= 1;
		if (carry || r >= d) {
			r -= d;
			q |= 1;
		}
	}
	return q;
}

static inline u32 lr(u32 reg) { return lapic[reg / 4]; }
static inline void lw(u32 reg, u32 v) { lapic[reg / 4] = v; }

void pic_disable(void)
{
	/* Remap so stray legacy IRQs cannot alias CPU exceptions, then mask all. */
	outb(0x20, 0x11); io_wait();
	outb(0xA0, 0x11); io_wait();
	outb(0x21, 0x20); io_wait();
	outb(0xA1, 0x28); io_wait();
	outb(0x21, 0x04); io_wait();
	outb(0xA1, 0x02); io_wait();
	outb(0x21, 0x01); io_wait();
	outb(0xA1, 0x01); io_wait();
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);
}

void lapic_init(void)
{
	u64 base = rdmsr(MSR_APIC_BASE);
	wrmsr(MSR_APIC_BASE, base | (1 << 11)); /* global enable */
	lapic = ioremap(acpi.lapic_phys, PAGE_SIZE);
	lw(LAPIC_SVR, 0x100 | VEC_SPURIOUS); /* software enable */
	KLOG("apic", "local APIC id %u enabled", lapic_id());
}

void lapic_eoi(void)
{
	if (lapic)
		lw(LAPIC_EOI, 0);
}

u32 lapic_id(void) { return lr(LAPIC_ID) >> 24; }

u64 msi_address(void) { return 0xFEE00000ULL | ((u64)lapic_id() << 12); }
u32 msi_data(u8 vector) { return vector; /* fixed delivery, edge */ }

/* ---- I/O APIC ---- */
static u32 io_read(u32 r)
{
	ioapic[0] = r;
	return ioapic[4];
}
static void io_write(u32 r, u32 v)
{
	ioapic[0] = r;
	ioapic[4] = v;
}

void ioapic_init(void)
{
	ioapic = ioremap(acpi.ioapics[0].phys, PAGE_SIZE);
	ioapic_gsi_base = acpi.ioapics[0].gsi_base;
	ioapic_max = ((io_read(1) >> 16) & 0xFF) + 1;
	for (u32 i = 0; i < ioapic_max; i++)
		io_write(0x10 + 2 * i, 1u << 16); /* masked */
	KLOG("apic", "I/O APIC with %u inputs", ioapic_max);
}

void ioapic_route_isa(u8 isa_irq, u8 vector)
{
	u32 gsi = acpi.isa_gsi[isa_irq] - ioapic_gsi_base;
	u16 fl = acpi.isa_flags[isa_irq];
	u32 lo = vector;
	if ((fl & 3) == 3)
		lo |= 1u << 13; /* active low */
	if (((fl >> 2) & 3) == 3)
		lo |= 1u << 15; /* level triggered */
	io_write(0x10 + 2 * gsi + 1, lapic_id() << 24);
	io_write(0x10 + 2 * gsi, lo);
}

void ioapic_mask_isa(u8 isa_irq, bool masked)
{
	u32 gsi = acpi.isa_gsi[isa_irq] - ioapic_gsi_base;
	u32 lo = io_read(0x10 + 2 * gsi);
	io_write(0x10 + 2 * gsi, masked ? lo | (1u << 16) : lo & ~(1u << 16));
}

/* ---- calibration with PIT channel 2 (speaker gate, no IRQ needed) ---- */
static void pit_wait_start(u16 count)
{
	outb(0x61, (inb(0x61) & ~0x02) | 0x01); /* gate on, speaker off */
	outb(0x43, 0xB0);			  /* ch2, lobyte/hibyte, mode 0 */
	outb(0x42, count & 0xFF);
	outb(0x42, count >> 8);
	/* restart the count by toggling the gate */
	u8 v = inb(0x61);
	outb(0x61, v & ~1);
	outb(0x61, v | 1);
}

static bool pit_done(void) { return inb(0x61) & 0x20; }

/* Fallback reference: the ACPI PM timer (3.579545 MHz; 35795 ticks = 10 ms).
 * Some recent machines no longer provide a working 8254 PIT. */
static bool pm_wait_10ms(u16 port, bool is32)
{
	u32 mask = is32 ? 0xFFFFFFFFu : 0xFFFFFFu;
	u32 start = inl(port) & mask;
	for (u64 spins = 0; spins < 100000000ULL; spins++) {
		u32 now = inl(port) & mask;
		if (((now - start) & mask) >= 35795)
			return true;
	}
	return false;
}

static const char *calib_source = "PIT channel 2";

void time_init(void)
{
	/* 10 ms reference interval, median of 3 runs. Reference: PIT channel 2
	 * (1.193182 MHz, 11932 ticks); if it never completes, or with the
	 * command line option time=pmtimer, the ACPI PM timer. */
	u64 tsc_s[3], lapic_s[3];
	bool is32;
	u16 pmt = acpi_pm_timer_port(&is32);
	bool use_pm = pmt && boot_cmdline_has("time=pmtimer");
	lw(LAPIC_TIMER_DIV, 0x3); /* divide by 16 */
	for (int k = 0; k < 3; k++) {
		lw(LAPIC_LVT_TIMER, 1u << 16); /* masked, one-shot */
		bool ok = false;
		u64 t0 = 0, t1 = 0;
		if (!use_pm) {
			pit_wait_start(11932);
			lw(LAPIC_TIMER_INIT, 0xFFFFFFFF);
			t0 = rdtsc();
			/* ~10 ms expected; give up after ~2^34 TSC cycles (seconds) */
			while (!(ok = pit_done()) && rdtsc() - t0 < (1ULL << 34))
				cpu_pause();
			t1 = rdtsc();
			if (!ok && pmt) {
				KLOG("time", "PIT channel 2 did not count; using the ACPI PM timer");
				use_pm = true;
			}
		}
		if (use_pm) {
			lw(LAPIC_TIMER_INIT, 0xFFFFFFFF);
			t0 = rdtsc();
			ok = pm_wait_10ms(pmt, is32);
			t1 = rdtsc();
			calib_source = "ACPI PM timer";
		}
		if (!ok)
			panic("time: no working calibration reference (PIT or ACPI PM timer)");
		u32 cur = lr(LAPIC_TIMER_CUR);
		tsc_s[k] = (t1 - t0) * 100;
		lapic_s[k] = (u64)(0xFFFFFFFF - cur) * 100;
	}
	for (int i = 0; i < 3; i++)
		for (int j = i + 1; j < 3; j++) {
			if (tsc_s[j] < tsc_s[i]) { u64 t = tsc_s[i]; tsc_s[i] = tsc_s[j]; tsc_s[j] = t; }
			if (lapic_s[j] < lapic_s[i]) { u64 t = lapic_s[i]; lapic_s[i] = lapic_s[j]; lapic_s[j] = t; }
		}
	tsc_hz = tsc_s[1];
	lapic_ticks_per_sec = lapic_s[1];
	tsc_boot = rdtsc();
	ns_mult = div128_64(1000000000ULL >> 32, 1000000000ULL << 32, tsc_hz);
	KLOG("time", "TSC %lu.%03lu MHz, LAPIC timer %lu kHz (div 16), calibrated with the %s",
	     tsc_hz / 1000000, (tsc_hz / 1000) % 1000, lapic_ticks_per_sec / 1000, calib_source);
}

u64 time_ns(void)
{
	if (!tsc_hz)
		return 0;
	return (u64)(((unsigned __int128)(rdtsc() - tsc_boot) * ns_mult) >> 32);
}
u64 time_ms(void) { return time_ns() / 1000000; }
u64 ticks(void) { return tick_count; }
i64 time_unix(void) { return boot.boot_timestamp + (i64)(time_ns() / 1000000000ULL); }
u64 tsc_frequency(void) { return tsc_hz; }
u32 timer_frequency(void) { return timer_hz; }
u64 lapic_timer_hz(void) { return lapic_ticks_per_sec; }

void delay_us(u64 us)
{
	u64 end = time_ns() + us * 1000;
	while (time_ns() < end)
		cpu_pause();
}

__attribute__((weak)) void sched_timer_tick(void) {}

static void timer_irq(struct trap_frame *tf, void *ctx)
{
	tick_count++;
	sched_timer_tick();
}

void timer_irq_setup(u32 hz)
{
	timer_hz = hz;
	irq_register(VEC_TIMER, timer_irq, NULL);
	lw(LAPIC_TIMER_DIV, 0x3);
	lw(LAPIC_LVT_TIMER, VEC_TIMER | (1u << 17)); /* periodic */
	lw(LAPIC_TIMER_INIT, (u32)(lapic_ticks_per_sec / hz));
	KLOG("time", "LAPIC periodic timer at %u Hz (vector 0x%x)", hz, VEC_TIMER);
}
