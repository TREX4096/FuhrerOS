// Freestanding C++ runtime support for the kernel: allocation operators on
// top of kmalloc, and the few ABI hooks GCC may reference. No exceptions,
// no RTTI, no libstdc++.
extern "C" {
#include "kernel.h"
#include "mm.h"
}

void *operator new(size_t n) { return kmalloc(n); }
void *operator new[](size_t n) { return kmalloc(n); }
void operator delete(void *p) noexcept { kfree(p); }
void operator delete[](void *p) noexcept { kfree(p); }
void operator delete(void *p, size_t) noexcept { kfree(p); }
void operator delete[](void *p, size_t) noexcept { kfree(p); }

extern "C" {
void __cxa_pure_virtual() { panic("pure virtual function called"); }
void *__dso_handle = nullptr;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; } // kernel never exits
}
