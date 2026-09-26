/* mkffs0 — build an FFS0 disk image from a host directory tree.
 *   mkffs0 IMAGE SIZE_MB SOURCE_DIR [LABEL]
 * Host tool (built with the host compiler); shares the on-disk format with
 * the kernel through kernel/include/ffs0_format.h. */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../kernel/include/ffs0_format.h"

static FILE *img;
static struct ffs0_super sb;
static uint8_t *bitmap, *ibitmap;
static uint64_t next_block;
static uint32_t next_ino = FFS0_ROOT_INO;

static void wblock(uint64_t b, const void *data)
{
	fseeko(img, (off_t)(b * FFS0_BLOCK), SEEK_SET);
	fwrite(data, FFS0_BLOCK, 1, img);
}
static void rblock(uint64_t b, void *data)
{
	fseeko(img, (off_t)(b * FFS0_BLOCK), SEEK_SET);
	if (fread(data, FFS0_BLOCK, 1, img) != 1)
		memset(data, 0, FFS0_BLOCK);
}

static uint32_t alloc_block(void)
{
	uint64_t b = next_block++;
	if (b >= sb.total_blocks) {
		fprintf(stderr, "mkffs0: image full\n");
		exit(1);
	}
	bitmap[b / 8] |= (uint8_t)(1 << (b % 8));
	sb.free_blocks--;
	static uint8_t zero[FFS0_BLOCK];
	wblock(b, zero);
	return (uint32_t)b;
}

static void put_inode(uint32_t ino, const struct ffs0_inode *in)
{
	uint8_t blk[FFS0_BLOCK];
	uint64_t b = sb.inode_table_start + ino / FFS0_INODES_PER_BLOCK;
	rblock(b, blk);
	memcpy(blk + (ino % FFS0_INODES_PER_BLOCK) * FFS0_INODE_SIZE, in, sizeof(*in));
	wblock(b, blk);
}

static uint32_t new_inode(uint16_t type)
{
	uint32_t ino = next_ino++;
	ibitmap[ino / 8] |= (uint8_t)(1 << (ino % 8));
	sb.free_inodes--;
	(void)type;
	return ino;
}

/* Store `len` bytes of data as the contents of inode `in`. */
static void store(struct ffs0_inode *in, const uint8_t *data, uint64_t len)
{
	uint64_t nblocks = (len + FFS0_BLOCK - 1) / FFS0_BLOCK;
	uint32_t ind[FFS0_PTRS_PER_BLOCK] = { 0 };
	for (uint64_t i = 0; i < nblocks; i++) {
		uint8_t blk[FFS0_BLOCK] = { 0 };
		uint64_t n = len - i * FFS0_BLOCK < FFS0_BLOCK ? len - i * FFS0_BLOCK : FFS0_BLOCK;
		memcpy(blk, data + i * FFS0_BLOCK, n);
		uint32_t b = alloc_block();
		wblock(b, blk);
		if (i < FFS0_NDIRECT) {
			in->direct[i] = b;
		} else if (i - FFS0_NDIRECT < FFS0_PTRS_PER_BLOCK) {
			ind[i - FFS0_NDIRECT] = b;
		} else {
			fprintf(stderr, "mkffs0: file too large for the image builder\n");
			exit(1);
		}
		in->blocks_used++;
	}
	if (nblocks > FFS0_NDIRECT) {
		in->indirect = alloc_block();
		wblock(in->indirect, ind);
	}
	in->size = len;
}

static uint32_t add_tree(const char *path, uint32_t parent_ino)
{
	(void)parent_ino;
	struct ffs0_inode dir = { .type = FFS0_T_DIR, .links = 1 };
	dir.mtime = dir.ctime = (uint64_t)time(NULL);
	uint32_t ino = new_inode(FFS0_T_DIR);
	size_t cap = 16, n = 0;
	struct ffs0_dirent *ents = calloc(cap, sizeof(*ents));
	DIR *d = opendir(path);
	struct dirent *de;
	while (d && (de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (strlen(de->d_name) > FFS0_NAME_MAX)
			continue;
		char full[4096];
		snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
		struct stat st;
		if (stat(full, &st))
			continue;
		if (n == cap)
			ents = realloc(ents, (cap *= 2) * sizeof(*ents));
		memset(&ents[n], 0, sizeof(ents[n]));
		strncpy(ents[n].name, de->d_name, FFS0_NAME_MAX);
		ents[n].name_len = (uint8_t)strlen(de->d_name);
		if (S_ISDIR(st.st_mode)) {
			ents[n].type = FFS0_T_DIR;
			ents[n].ino = add_tree(full, ino);
		} else if (S_ISREG(st.st_mode)) {
			ents[n].type = FFS0_T_FILE;
			struct ffs0_inode f = { .type = FFS0_T_FILE, .links = 1 };
			f.mtime = f.ctime = (uint64_t)st.st_mtime;
			uint32_t fino = new_inode(FFS0_T_FILE);
			FILE *fp = fopen(full, "rb");
			uint8_t *buf = malloc((size_t)st.st_size + 1);
			size_t got = fp ? fread(buf, 1, (size_t)st.st_size, fp) : 0;
			if (fp)
				fclose(fp);
			store(&f, buf, got);
			free(buf);
			put_inode(fino, &f);
			ents[n].ino = fino;
		} else {
			continue;
		}
		n++;
	}
	if (d)
		closedir(d);
	store(&dir, (uint8_t *)ents, n * sizeof(*ents));
	put_inode(ino, &dir);
	free(ents);
	return ino;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: mkffs0 IMAGE SIZE_MB SOURCE_DIR [LABEL]\n");
		return 2;
	}
	uint64_t size = strtoull(argv[2], NULL, 10) << 20;
	img = fopen(argv[1], "w+b");
	if (!img) {
		perror(argv[1]);
		return 1;
	}
	if (ftruncate(fileno(img), (off_t)size)) {
		perror("ftruncate");
		return 1;
	}
	memset(&sb, 0, sizeof(sb));
	sb.magic = FFS0_MAGIC;
	sb.version = FFS0_VERSION;
	sb.block_size = FFS0_BLOCK;
	sb.total_blocks = size / FFS0_BLOCK;
	sb.inode_count = 4096;
	sb.bitmap_start = 1;
	sb.bitmap_blocks = (uint32_t)((sb.total_blocks + FFS0_BLOCK * 8 - 1) / (FFS0_BLOCK * 8));
	sb.inode_bitmap_start = sb.bitmap_start + sb.bitmap_blocks;
	sb.inode_table_start = sb.inode_bitmap_start + 1;
	sb.inode_table_blocks = sb.inode_count / FFS0_INODES_PER_BLOCK;
	sb.data_start = sb.inode_table_start + sb.inode_table_blocks;
	sb.root_ino = FFS0_ROOT_INO;
	sb.free_blocks = sb.total_blocks;
	sb.free_inodes = sb.inode_count - 1; /* inode 0 is reserved */
	sb.created = (uint64_t)time(NULL);
	sb.clean = 1;
	snprintf(sb.label, sizeof(sb.label), "%s", argc > 4 ? argv[4] : "fuhreros");
	bitmap = calloc(sb.bitmap_blocks, FFS0_BLOCK);
	ibitmap = calloc(1, FFS0_BLOCK);
	ibitmap[0] |= 1; /* inode 0 */
	for (uint64_t b = 0; b < sb.data_start; b++) {
		bitmap[b / 8] |= (uint8_t)(1 << (b % 8));
		sb.free_blocks--;
	}
	next_block = sb.data_start;
	uint32_t root = add_tree(argv[3], 0);
	if (root != FFS0_ROOT_INO) {
		fprintf(stderr, "mkffs0: unexpected root inode %u\n", root);
		return 1;
	}
	for (uint32_t i = 0; i < sb.bitmap_blocks; i++)
		wblock(sb.bitmap_start + i, bitmap + (size_t)i * FFS0_BLOCK);
	wblock(sb.inode_bitmap_start, ibitmap);
	uint8_t blk[FFS0_BLOCK] = { 0 };
	memcpy(blk, &sb, sizeof(sb));
	wblock(0, blk);
	fclose(img);
	printf("mkffs0: %s: %lu MiB, %u inodes used, %lu/%lu blocks free\n", argv[1],
	       (unsigned long)(size >> 20), next_ino - 1, (unsigned long)sb.free_blocks,
	       (unsigned long)sb.total_blocks);
	return 0;
}
