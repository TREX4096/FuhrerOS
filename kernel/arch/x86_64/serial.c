/* 16550 UART on COM1 — the earliest (and most reliable) debug channel. */
#include "arch/x86_64/serial.h"
#include "arch/x86_64/cpu.h"

#define COM1 0x3F8

static bool present;

void serial_init(void)
{
	outb(COM1 + 1, 0x00);	/* no interrupts */
	outb(COM1 + 3, 0x80);	/* DLAB */
	outb(COM1 + 0, 0x01);	/* 115200 baud */
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);	/* 8N1 */
	outb(COM1 + 2, 0xC7);	/* FIFO, clear, 14-byte threshold */
	outb(COM1 + 4, 0x0B);
	/* Loopback self-test: a missing UART must not hang the kernel. */
	outb(COM1 + 4, 0x1E);
	outb(COM1 + 0, 0xAE);
	present = inb(COM1 + 0) == 0xAE;
	outb(COM1 + 4, 0x0F);
}

void serial_putc(char c)
{
	if (!present)
		return;
	if (c == '\n')
		serial_putc('\r');
	for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++)
		cpu_pause();
	outb(COM1, (u8)c);
}

void serial_write(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		serial_putc(s[i]);
}

int serial_getc_nonblock(void)
{
	if (!present || !(inb(COM1 + 5) & 1))
		return -1;
	return inb(COM1);
}

void serial_enable_rx_irq(void)
{
	if (present)
		outb(COM1 + 1, 0x01);
}
