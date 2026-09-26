#ifndef ARCH_SERIAL_H
#define ARCH_SERIAL_H
#include "types.h"
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s, size_t n);
int serial_getc_nonblock(void);
void serial_enable_rx_irq(void);
#endif
