/* Compatibility shim: lets kernel/lib/{string,printf}.c build for user space. */
#ifndef FU_LIB_SHIM_H
#define FU_LIB_SHIM_H
#include "fu.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t i64;
#endif
