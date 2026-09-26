/* Kernel symbol lookup for stack traces. The table is generated from the
 * first-pass link (scripts/gen-ksyms.sh) and linked in the second pass; in
 * the first pass these weak empty definitions are used. */
#include "kernel.h"

struct ksym {
	u64 addr;
	const char *name;
};

__attribute__((weak)) const struct ksym ksyms_table[] = { { 0, 0 } };
__attribute__((weak)) const u64 ksyms_count = 0;

const char *ksym_lookup(u64 addr, u64 *offset)
{
	const char *best = NULL;
	u64 best_addr = 0;
	/* Table is sorted by address: binary search for the last entry <= addr. */
	u64 lo = 0, hi = ksyms_count;
	while (lo < hi) {
		u64 mid = (lo + hi) / 2;
		if (ksyms_table[mid].addr <= addr) {
			best = ksyms_table[mid].name;
			best_addr = ksyms_table[mid].addr;
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (offset)
		*offset = addr - best_addr;
	return best;
}
