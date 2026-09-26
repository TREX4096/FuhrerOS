/* Freestanding string/memory routines. The compiler may emit calls to
 * memset/memcpy/memmove/memcmp itself, so these must exist. */
#include "lib.h"

void *memset(void *d, int c, size_t n)
{
	u8 *p = d;
	if (n >= 16 && !((uintptr_t)p & 7)) {
		u64 v = (u8)c;
		v |= v << 8;
		v |= v << 16;
		v |= v << 32;
		while (n >= 8) {
			*(u64 *)p = v;
			p += 8;
			n -= 8;
		}
	}
	while (n--)
		*p++ = (u8)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	void *ret = d;
	__asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n)::"memory");
	return ret;
}

void *memmove(void *d, const void *s, size_t n)
{
	u8 *dp = d;
	const u8 *sp = s;
	if (dp == sp || n == 0)
		return d;
	if (dp < sp || dp >= sp + n)
		return memcpy(d, s, n);
	dp += n;
	sp += n;
	while (n--)
		*--dp = *--sp;
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const u8 *x = a, *y = b;
	for (size_t i = 0; i < n; i++)
		if (x[i] != y[i])
			return x[i] < y[i] ? -1 : 1;
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

size_t strnlen(const char *s, size_t max)
{
	size_t n = 0;
	while (n < max && s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b || !*a)
			return (u8)*a - (u8)*b;
	}
	return 0;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

size_t strlcpy(char *d, const char *s, size_t n)
{
	size_t len = strlen(s);
	if (n) {
		size_t c = len < n - 1 ? len : n - 1;
		memcpy(d, s, c);
		d[c] = 0;
	}
	return len;
}

size_t strlcat(char *d, const char *s, size_t n)
{
	size_t dl = strnlen(d, n);
	if (dl == n)
		return n + strlen(s);
	return dl + strlcpy(d + dl, s, n - dl);
}

char *strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == (char)c)
			return (char *)s;
	return c ? NULL : (char *)s;
}

char *strrchr(const char *s, int c)
{
	const char *r = NULL;
	for (; *s; s++)
		if (*s == (char)c)
			r = s;
	return c ? (char *)r : (char *)s;
}

unsigned long strtoul(const char *s, char **end, int base)
{
	unsigned long v = 0;
	while (*s == ' ')
		s++;
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = 10;
	for (;; s++) {
		int d;
		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (*s >= 'a' && *s <= 'z')
			d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')
			d = *s - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		v = v * (unsigned long)base + (unsigned long)d;
	}
	if (end)
		*end = (char *)s;
	return v;
}

long strtol(const char *s, char **end, int base)
{
	while (*s == ' ')
		s++;
	if (*s == '-')
		return -(long)strtoul(s + 1, end, base);
	return (long)strtoul(s, end, base);
}
