/* Kernel console: printk fans out to the serial port, the framebuffer
 * console, and an in-memory log ring (exposed as /proc/kmsg). */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/serial.h"
#include "gfx/fb.h"
#include "kernel.h"

#define KLOG_SIZE (64 * 1024)
static char klog[KLOG_SIZE];
static u64 klog_head;	/* total bytes ever written */
static bool fb_on = true;

void console_set_fb(bool on) { fb_on = on; }

void console_write(const char *s, size_t n)
{
	u64 f = irq_save();
	serial_write(s, n);
	if (fb_on)
		fbcon_write(s, n);
	for (size_t i = 0; i < n; i++)
		klog[(klog_head + i) % KLOG_SIZE] = s[i];
	klog_head += n;
	irq_restore(f);
}

/* Copy up to len bytes of the most recent log into buf; returns count. */
size_t klog_read(char *buf, size_t len, u64 offset)
{
	u64 start = klog_head > KLOG_SIZE ? klog_head - KLOG_SIZE : 0;
	if (offset < start)
		offset = start;
	size_t n = 0;
	while (offset + n < klog_head && n < len) {
		buf[n] = klog[(offset + n) % KLOG_SIZE];
		n++;
	}
	return n;
}

u64 klog_size(void) { return klog_head; }

void vprintk(const char *fmt, va_list ap)
{
	char buf[512];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (n > (int)sizeof(buf) - 1)
		n = sizeof(buf) - 1;
	console_write(buf, (size_t)n);
}

void printk(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}
