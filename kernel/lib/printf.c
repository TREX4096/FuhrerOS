/* Minimal vsnprintf: %d %i %u %x %X %p %s %c %% with flags '-', '0',
 * width, precision (strings), and length modifiers l / ll / z. Integer only:
 * the kernel never touches the FPU. Shared with user space (libfuhrer). */
#include "lib.h"

struct out {
	char *buf;
	size_t cap;
	size_t len;
};

static void put(struct out *o, char c)
{
	if (o->len + 1 < o->cap)
		o->buf[o->len] = c;
	o->len++;
}

static void put_num(struct out *o, u64 v, int base, bool upper, bool neg, int width, bool zero,
		    bool left)
{
	char tmp[24];
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int n = 0;
	do {
		tmp[n++] = digits[v % (unsigned)base];
		v /= (unsigned)base;
	} while (v);
	int total = n + (neg ? 1 : 0);
	if (!left && !zero)
		for (; total < width; total++)
			put(o, ' ');
	if (neg)
		put(o, '-');
	if (!left && zero)
		for (; total < width; total++)
			put(o, '0');
	while (n)
		put(o, tmp[--n]);
	if (left)
		for (; total < width; total++)
			put(o, ' ');
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
	struct out o = { buf, cap, 0 };
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			put(&o, *fmt);
			continue;
		}
		fmt++;
		bool left = false, zero = false;
		for (;; fmt++) {
			if (*fmt == '-')
				left = true;
			else if (*fmt == '0')
				zero = true;
			else
				break;
		}
		int width = 0, prec = -1;
		if (*fmt == '*') {
			width = va_arg(ap, int);
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				fmt++;
			}
			while (*fmt >= '0' && *fmt <= '9')
				prec = prec * 10 + (*fmt++ - '0');
		}
		int lng = 0;
		while (*fmt == 'l') {
			lng++;
			fmt++;
		}
		if (*fmt == 'z') {
			lng = 2;
			fmt++;
		}
		switch (*fmt) {
		case 'd':
		case 'i': {
			i64 v = lng ? va_arg(ap, i64) : va_arg(ap, int);
			put_num(&o, v < 0 ? (u64)(-v) : (u64)v, 10, false, v < 0, width, zero, left);
			break;
		}
		case 'u':
		case 'x':
		case 'X': {
			u64 v = lng ? va_arg(ap, u64) : va_arg(ap, unsigned);
			put_num(&o, v, *fmt == 'u' ? 10 : 16, *fmt == 'X', false, width, zero, left);
			break;
		}
		case 'p':
			put(&o, '0');
			put(&o, 'x');
			put_num(&o, (u64)va_arg(ap, void *), 16, false, false, 16, true, false);
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			int l = (int)strlen(s);
			if (prec >= 0 && l > prec)
				l = prec;
			if (!left)
				for (int i = l; i < width; i++)
					put(&o, ' ');
			for (int i = 0; i < l; i++)
				put(&o, s[i]);
			if (left)
				for (int i = l; i < width; i++)
					put(&o, ' ');
			break;
		}
		case 'c':
			put(&o, (char)va_arg(ap, int));
			break;
		case '%':
			put(&o, '%');
			break;
		default:
			put(&o, '%');
			put(&o, *fmt);
			break;
		}
	}
	if (cap)
		buf[o.len < cap ? o.len : cap - 1] = 0;
	return (int)o.len;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return r;
}
