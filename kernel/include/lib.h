/* Freestanding C library subset used inside the kernel. */
#ifndef FUHRER_LIB_H
#define FUHRER_LIB_H

#include "types.h"

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
size_t strlcpy(char *d, const char *s, size_t n);
size_t strlcat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* 64/64 integer helpers that never touch the FPU. */
static inline u64 div_round(u64 a, u64 b) { return b ? (a + b / 2) / b : 0; }

#endif
