/* FFS0 — FuhrerOS File System, version 0: on-disk format (shared by the
 * kernel driver fs/ffs0.c and the host tool tools/mkffs0.c).
 *
 *   block 0                superblock
 *   bitmap_start ...       block allocation bitmap (1 bit / block)
 *   inode_bitmap_start     inode allocation bitmap (one block)
 *   inode_table_start ...  inodes, 32 per 4 KiB block (128 B each)
 *   data_start ...         file and directory data
 *
 * Inodes address 12 direct blocks, one single-indirect and one double-
 * indirect block (1024 entries each): max file size ~4 GiB. Directories are
 * files of 64-byte entries; ino 0 marks a free slot. Deliberately simple so
 * its behaviour is easy to reason about in experiments (NEW_EXPLANATION §20).
 * No journal yet: a crash can leak blocks (fsck is future work). */
#ifndef FFS0_FORMAT_H
#define FFS0_FORMAT_H

#include <stdint.h>

#define FFS0_MAGIC 0x3053464652484655ULL /* "UHRFFS0" */
#define FFS0_VERSION 1
#define FFS0_BLOCK 4096
#define FFS0_INODE_SIZE 128
#define FFS0_INODES_PER_BLOCK (FFS0_BLOCK / FFS0_INODE_SIZE)
#define FFS0_NDIRECT 12
#define FFS0_PTRS_PER_BLOCK (FFS0_BLOCK / 4)
#define FFS0_DIRENT_SIZE 64
#define FFS0_NAME_MAX 58
#define FFS0_ROOT_INO 1

#define FFS0_T_FILE 1
#define FFS0_T_DIR 2

struct ffs0_super {
	uint64_t magic;
	uint32_t version;
	uint32_t block_size;
	uint64_t total_blocks;
	uint32_t inode_count;
	uint32_t bitmap_start;
	uint32_t bitmap_blocks;
	uint32_t inode_bitmap_start;
	uint32_t inode_table_start;
	uint32_t inode_table_blocks;
	uint32_t data_start;
	uint32_t root_ino;
	uint64_t free_blocks;
	uint32_t free_inodes;
	uint32_t mount_count;
	uint64_t created;
	uint64_t last_mount;
	char label[32];
	uint8_t clean;
	uint8_t pad[3];
};

struct ffs0_inode {
	uint16_t type;
	uint16_t links;
	uint32_t blocks_used;
	uint64_t size;
	uint64_t mtime;
	uint64_t ctime;
	uint32_t direct[FFS0_NDIRECT];
	uint32_t indirect;
	uint32_t dindirect;
	uint8_t pad[128 - 2 - 2 - 4 - 8 - 8 - 8 - 4 * FFS0_NDIRECT - 4 - 4];
};

struct ffs0_dirent {
	uint32_t ino;
	uint8_t type;
	uint8_t name_len;
	char name[FFS0_NAME_MAX];
};

_Static_assert(sizeof(struct ffs0_inode) == FFS0_INODE_SIZE, "inode size");
_Static_assert(sizeof(struct ffs0_dirent) == FFS0_DIRENT_SIZE, "dirent size");

#endif
