/* Storage syscalls: block/cache statistics and cache-policy control (M14). */
#include "blk.h"
#include "kernel.h"
#include "proc.h"
#include "uapi/fuhrer.h"

i64 sys_blk(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e)
{
	if (nr == SYS_BLKSTAT) {
		struct fu_blkstat s;
		memset(&s, 0, sizeof(s));
		struct blkdev *dv = blk_get("vda");
		if (dv) {
			s.reads = dv->reads;
			s.writes = dv->writes;
			s.read_bytes = dv->read_bytes;
			s.write_bytes = dv->write_bytes;
		}
		struct bcache_stats bs;
		bcache_get_stats(&bs);
		s.cache_hits = bs.hits;
		s.cache_misses = bs.misses;
		s.readahead_blocks = bs.readahead_issued;
		s.readahead_hits = bs.readahead_used;
		s.evictions = bs.evictions;
		s.cache_blocks = bs.blocks;
		s.cache_capacity = bs.capacity;
		strlcpy(s.cache_policy, bcache_policy_name(), sizeof(s.cache_policy));
		strlcpy(s.io_class, bs.io_class, sizeof(s.io_class));
		return copy_to_user((void *)a, &s, sizeof(s));
	}
	switch (a) {
	case CACHE_SET_POLICY: {
		char name[24];
		if (strncpy_from_user(name, (const char *)c, sizeof(name)) < 0)
			return -E_FAULT;
		return bcache_set_policy(name);
	}
	case CACHE_SET_CAPACITY: bcache_set_capacity((u32)b); return 0;
	case CACHE_FLUSH: bsync(NULL); return 0;
	case CACHE_RESET_STATS: bcache_reset_stats(); return 0;
	case CACHE_DROP: bcache_drop(); return 0;
	}
	return -E_INVAL;
}
