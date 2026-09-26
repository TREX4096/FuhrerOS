# Storage and filesystems

## Block layer
- **PCI** (`drivers/pci.c`): configuration mechanism #1, BAR sizing, MSI-X.
- **virtio 1.x** (`drivers/virtio/virtio.c`): modern PCI transport,
  capability parsing, feature negotiation (`VERSION_1`), split virtqueues in
  DMA frames, MSI-X completion interrupts.
- **virtio-blk** (`drivers/virtio/blk.c`): asynchronous requests with
  completion callbacks, per-device statistics (`/proc/blk`).

## Buffer cache (`fs/bcache.c`, research component M14)
4 KiB blocks, each a DMA frame; hash lookup; write-back (dirty blocks written
on eviction, `sync`, and every 2 s). Policies (D-112): `lru`, `fifo`,
`clock`, `readahead` (sequential window 2→32 blocks), `adaptive` (stream
classification every 256 accesses; sequential → aggressive read-ahead +
evict-behind; random → no read-ahead, CLOCK when a working set shows up;
mixed → window 4). `/proc/bcache` and `cachectl` expose policy, capacity,
hit rate, read-ahead issued/used, evictions, write-backs, memory and CPU time.

## VFS (`fs/vfs.c`)
Vnodes with an operations table, mount table with longest-prefix match,
normalised absolute paths, open-file objects with offsets.

| mount | filesystem |
|---|---|
| `/` | FFS0 on `vda` (falls back to the initrd when no disk is present) |
| `/initrd` | tarfs — the ustar boot module, read-only, zero-copy |
| `/dev` | devfs — console, tty, null, zero, random |
| `/proc` | procfs — tasks, sched, adapt, meminfo, cpuinfo, bcache, blk, net, pci, desktop, kmsg, mounts, interrupts, version, cmdline, uptime |
| `/tmp` | ramfs |

## FFS0 (`fs/ffs0.c`, format in `include/ffs0_format.h`)
Block 0 superblock; block bitmap; inode bitmap; inode table (128-byte inodes,
12 direct + single + double indirect blocks, ~4 GiB max file); directories of
64-byte entries. Operations: lookup, readdir, create (files/directories),
unlink (deferred free while open), rename, truncate, read, write (sparse
holes read as zeros). `tools/mkffs0` builds the 20 GiB sparse root image
from the staged root filesystem at build time. No journal (D-111).

**Durability.** The virtio-blk device runs with a write cache (the driver
negotiates `VIRTIO_BLK_F_FLUSH`, F-123). Data is durable after `sync()` or
poweroff, which write back dirty buffers and issue a device FLUSH, or after
the `bflush` thread's next pass (every 2 s). A crash can therefore lose up
to about 2 s of writes, and without a journal it can leave metadata
inconsistent. `/proc/blk` reports `write_cache` and the number of flushes.

Tested by `usertest`: create/write/stat, cp, mv, rm, 1 MiB checksummed file
(indirect blocks), rmdir.
