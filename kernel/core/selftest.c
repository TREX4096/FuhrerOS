/* In-kernel self tests. Booting with the `test` command-line word makes every
 * stage report "TEST <name> PASS|FAIL <detail>" on the serial console and
 * exit QEMU through the isa-debug-exit device when finished; scripts/test.sh
 * parses the output. Outside test mode results are only logged. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"

static int passed, failed;

bool selftest_mode(void) { return boot_cmdline_has("test"); }

void selftest_report(const char *name, bool ok, const char *fmt, ...)
{
	char detail[160] = "";
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(detail, sizeof(detail), fmt, ap);
		va_end(ap);
	}
	if (ok)
		passed++;
	else
		failed++;
	printk("TEST %-28s %s %s\n", name, ok ? "PASS" : "FAIL", detail);
}

void selftest_summary(void)
{
	printk("TEST-SUMMARY passed=%d failed=%d\n", passed, failed);
}

int selftest_failures(void) { return failed; }

/* QEMU `-device isa-debug-exit,iobase=0xf4`: exit status = (code << 1) | 1. */
NORETURN void qemu_exit(int code)
{
	outl(0xf4, (u32)code);
	for (;;) {
		cli();
		hlt();
	}
}
