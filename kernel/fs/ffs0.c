/* FFS0 filesystem driver (M7). All metadata and data go through the buffer
 * cache, so cache policy experiments (M14) see real filesystem traffic. */
#include "arch/x86_64/cpu.h"
#include "blk.h"
#include "ffs0_format.h"
#include "kernel.h"
#include "mm.h"
#include "sched.h"
#include "vfs.h"

struct ffs0 {
	struct blkdev *dev;
	struct ffs0_super sb;
	struct mutex lock;
	struct list_node vnodes;	/* open inode cache */
	u32 alloc_hint;
};

struct fnode {
	struct vnode vn;
	struct ffs0 *fs;
	u32 ino;
	struct list_node node;
};

static const struct vnode_ops ffs0_ops;

/* ---- block helpers ---- */
static struct buf *blk(struct ffs0 *fs, u64 b) { return bread(fs->dev, b); }

static void write_super(struct ffs0 *fs)
{
	struct buf *b = blk(fs, 0);
	memcpy(b->data, &fs->sb, sizeof(fs->sb));
	bdirty(b);
	brelse(b);
}

static int inode_rw(struct ffs0 *fs, u32 ino, struct ffs0_inode *in, bool write)
{
	if (ino == 0 || ino >= fs->sb.inode_count)
		return -E_INVAL;
	u64 bno = fs->sb.inode_table_start + ino / FFS0_INODES_PER_BLOCK;
	struct buf *b = blk(fs, bno);
	struct ffs0_inode *slot = (struct ffs0_inode *)b->data + ino % FFS0_INODES_PER_BLOCK;
	if (write) {
		*slot = *in;
		bdirty(b);
	} else {
		*in = *slot;
	}
	brelse(b);
	return 0;
}

static u64 alloc_block(struct ffs0 *fs)
{
	u64 total = fs->sb.total_blocks;
	for (u64 k = 0; k < total; k++) {
		u64 bno = (fs->alloc_hint + k) % total;
		if (bno < fs->sb.data_start)
			continue;
		struct buf *b = blk(fs, fs->sb.bitmap_start + bno / (FFS0_BLOCK * 8));
		u64 bit = bno % (FFS0_BLOCK * 8);
		if (!(b->data[bit / 8] & (1 << (bit % 8)))) {
			b->data[bit / 8] |= (u8)(1 << (bit % 8));
			bdirty(b);
			brelse(b);
			fs->sb.free_blocks--;
			fs->alloc_hint = (u32)(bno + 1);
			struct buf *z = bget_nofill(fs->dev, bno); /* zero-filled */
			bdirty(z);
			brelse(z);
			return bno;
		}
		brelse(b);
	}
	return 0;
}

static void free_block(struct ffs0 *fs, u64 bno)
{
	if (!bno)
		return;
	struct buf *b = blk(fs, fs->sb.bitmap_start + bno / (FFS0_BLOCK * 8));
	u64 bit = bno % (FFS0_BLOCK * 8);
	b->data[bit / 8] &= (u8)~(1 << (bit % 8));
	bdirty(b);
	brelse(b);
	fs->sb.free_blocks++;
}

static u32 alloc_inode(struct ffs0 *fs, u16 type)
{
	struct buf *b = blk(fs, fs->sb.inode_bitmap_start);
	for (u32 i = 1; i < fs->sb.inode_count; i++) {
		if (!(b->data[i / 8] & (1 << (i % 8)))) {
			b->data[i / 8] |= (u8)(1 << (i % 8));
			bdirty(b);
			brelse(b);
			struct ffs0_inode in;
			memset(&in, 0, sizeof(in));
			in.type = type;
			in.links = 1;
			in.mtime = in.ctime = (u64)time_unix();
			inode_rw(fs, i, &in, true);
			fs->sb.free_inodes--;
			return i;
		}
	}
	brelse(b);
	return 0;
}

static void free_inode_num(struct ffs0 *fs, u32 ino)
{
	struct buf *b = blk(fs, fs->sb.inode_bitmap_start);
	b->data[ino / 8] &= (u8)~(1 << (ino % 8));
	bdirty(b);
	brelse(b);
	fs->sb.free_inodes++;
}

/* Map file block index -> disk block, allocating if `alloc`. */
static u64 bmap(struct ffs0 *fs, struct ffs0_inode *in, u64 idx, bool alloc, bool *changed)
{
	if (idx < FFS0_NDIRECT) {
		if (!in->direct[idx] && alloc) {
			in->direct[idx] = (u32)alloc_block(fs);
			in->blocks_used++;
			*changed = true;
		}
		return in->direct[idx];
	}
	idx -= FFS0_NDIRECT;
	u32 *top;
	u64 level2_idx = 0;
	bool dbl = false;
	if (idx < FFS0_PTRS_PER_BLOCK) {
		top = &in->indirect;
	} else {
		idx -= FFS0_PTRS_PER_BLOCK;
		if (idx >= (u64)FFS0_PTRS_PER_BLOCK * FFS0_PTRS_PER_BLOCK)
			return 0;
		top = &in->dindirect;
		dbl = true;
		level2_idx = idx % FFS0_PTRS_PER_BLOCK;
		idx /= FFS0_PTRS_PER_BLOCK;
	}
	if (!*top) {
		if (!alloc)
			return 0;
		*top = (u32)alloc_block(fs);
		*changed = true;
		if (!*top)
			return 0;
	}
	struct buf *b = blk(fs, *top);
	u32 *ptrs = (u32 *)b->data;
	if (!ptrs[idx] && alloc) {
		ptrs[idx] = (u32)alloc_block(fs);
		bdirty(b);
		if (!dbl) {
			in->blocks_used++;
			*changed = true;
		}
	}
	u64 r = ptrs[idx];
	brelse(b);
	if (!dbl || !r)
		return r;
	b = blk(fs, r);
	ptrs = (u32 *)b->data;
	if (!ptrs[level2_idx] && alloc) {
		ptrs[level2_idx] = (u32)alloc_block(fs);
		bdirty(b);
		in->blocks_used++;
		*changed = true;
	}
	r = ptrs[level2_idx];
	brelse(b);
	return r;
}

static void free_all_blocks(struct ffs0 *fs, struct ffs0_inode *in)
{
	for (int i = 0; i < FFS0_NDIRECT; i++) {
		free_block(fs, in->direct[i]);
		in->direct[i] = 0;
	}
	if (in->indirect) {
		struct buf *b = blk(fs, in->indirect);
		for (int i = 0; i < FFS0_PTRS_PER_BLOCK; i++)
			free_block(fs, ((u32 *)b->data)[i]);
		brelse(b);
		free_block(fs, in->indirect);
		in->indirect = 0;
	}
	if (in->dindirect) {
		struct buf *b = blk(fs, in->dindirect);
		for (int i = 0; i < FFS0_PTRS_PER_BLOCK; i++) {
			u32 l2 = ((u32 *)b->data)[i];
			if (!l2)
				continue;
			struct buf *c = blk(fs, l2);
			for (int j = 0; j < FFS0_PTRS_PER_BLOCK; j++)
				free_block(fs, ((u32 *)c->data)[j]);
			brelse(c);
			free_block(fs, l2);
		}
		brelse(b);
		free_block(fs, in->dindirect);
		in->dindirect = 0;
	}
	in->blocks_used = 0;
}

/* ---- vnodes ---- */
static struct vnode *get_vnode(struct ffs0 *fs, u32 ino)
{
	list_for_each(it, &fs->vnodes) {
		struct fnode *f = container_of(it, struct fnode, node);
		if (f->ino == ino) {
			vnode_ref(&f->vn);
			return &f->vn;
		}
	}
	struct ffs0_inode in;
	if (inode_rw(fs, ino, &in, false) < 0 || !in.type)
		return NULL;
	struct fnode *f = kzalloc(sizeof(*f));
	f->fs = fs;
	f->ino = ino;
	f->vn.type = in.type == FFS0_T_DIR ? VT_DIR : VT_FILE;
	f->vn.ino = ino;
	f->vn.size = in.size;
	f->vn.mtime = in.mtime;
	f->vn.nlink = in.links;
	f->vn.ops = &ffs0_ops;
	f->vn.priv = f;
	f->vn.refcnt = 1;
	list_push_back(&fs->vnodes, &f->node);
	return &f->vn;
}

static void f_release(struct vnode *vn)
{
	struct fnode *f = vn->priv;
	mutex_lock(&f->fs->lock);
	list_remove(&f->node);
	struct ffs0_inode in;
	if (inode_rw(f->fs, f->ino, &in, false) == 0 && in.links == 0) {
		/* last reference to an unlinked file: now free it */
		free_all_blocks(f->fs, &in);
		in.type = 0;
		inode_rw(f->fs, f->ino, &in, true);
		free_inode_num(f->fs, f->ino);
	}
	mutex_unlock(&f->fs->lock);
	kfree(f);
}

static ssize_t rw_locked(struct fnode *f, void *buf, u64 len, u64 off, bool write)
{
	struct ffs0 *fs = f->fs;
	struct ffs0_inode in;
	inode_rw(fs, f->ino, &in, false);
	if (!write) {
		if (off >= in.size)
			return 0;
		len = MIN(len, in.size - off);
	}
	u64 done = 0;
	bool changed = false;
	while (done < len) {
		u64 pos = off + done;
		u64 bi = pos / FFS0_BLOCK, bo = pos % FFS0_BLOCK;
		u64 n = MIN(len - done, FFS0_BLOCK - bo);
		u64 bno = bmap(fs, &in, bi, write, &changed);
		if (write) {
			if (!bno)
				break; /* disk full */
			struct buf *b = (bo == 0 && n == FFS0_BLOCK) ? bget_nofill(fs->dev, bno) : blk(fs, bno);
			memcpy(b->data + bo, (u8 *)buf + done, n);
			bdirty(b);
			brelse(b);
		} else if (!bno) {
			memset((u8 *)buf + done, 0, n); /* sparse hole */
		} else {
			struct buf *b = blk(fs, bno);
			memcpy((u8 *)buf + done, b->data + bo, n);
			brelse(b);
		}
		done += n;
	}
	if (write) {
		if (off + done > in.size) {
			in.size = off + done;
			changed = true;
		}
		in.mtime = (u64)time_unix();
		inode_rw(fs, f->ino, &in, true);
		f->vn.size = in.size;
		f->vn.mtime = in.mtime;
		if (done < len && done == 0)
			return -E_NOSPC;
	}
	(void)changed;
	return (ssize_t)done;
}

static ssize_t f_read(struct vnode *vn, void *buf, u64 len, u64 off, int flags)
{
	struct fnode *f = vn->priv;
	mutex_lock(&f->fs->lock);
	ssize_t r = rw_locked(f, buf, len, off, false);
	mutex_unlock(&f->fs->lock);
	return r;
}

static ssize_t f_write(struct vnode *vn, const void *buf, u64 len, u64 off, int flags)
{
	struct fnode *f = vn->priv;
	mutex_lock(&f->fs->lock);
	ssize_t r = rw_locked(f, (void *)buf, len, off, true);
	mutex_unlock(&f->fs->lock);
	return r;
}

/* Iterate directory entries; cb returns true to stop. */
static int dir_scan(struct fnode *d, bool (*cb)(struct ffs0_dirent *e, u64 off, void *ctx), void *ctx)
{
	struct ffs0_dirent e;
	for (u64 off = 0;; off += sizeof(e)) {
		ssize_t n = rw_locked(d, &e, sizeof(e), off, false);
		if (n < (ssize_t)sizeof(e))
			return 0;
		if (cb(&e, off, ctx))
			return 1;
	}
}

struct find_ctx {
	const char *name;
	u32 ino;
	u64 off;
	u8 type;
};
static bool find_cb(struct ffs0_dirent *e, u64 off, void *c)
{
	struct find_ctx *fc = c;
	if (e->ino && e->name_len == strlen(fc->name) && !memcmp(e->name, fc->name, e->name_len)) {
		fc->ino = e->ino;
		fc->off = off;
		fc->type = e->type;
		return true;
	}
	return false;
}

static int f_lookup(struct vnode *dir, const char *name, struct vnode **out)
{
	struct fnode *d = dir->priv;
	mutex_lock(&d->fs->lock);
	struct find_ctx fc = { .name = name };
	bool found = dir_scan(d, find_cb, &fc);
	struct vnode *vn = found ? get_vnode(d->fs, fc.ino) : NULL;
	mutex_unlock(&d->fs->lock);
	if (!vn)
		return -E_NOENT;
	vn->mnt = dir->mnt;
	*out = vn;
	return 0;
}

struct idx_ctx {
	u64 want, seen;
	struct dirent *out;
};
static bool idx_cb(struct ffs0_dirent *e, u64 off, void *c)
{
	struct idx_ctx *ic = c;
	if (!e->ino)
		return false;
	if (ic->seen++ == ic->want) {
		ic->out->ino = e->ino;
		ic->out->type = e->type == FFS0_T_DIR ? VT_DIR : VT_FILE;
		memcpy(ic->out->name, e->name, e->name_len);
		ic->out->name[e->name_len] = 0;
		return true;
	}
	return false;
}

static int f_readdir(struct vnode *dir, u64 index, struct dirent *out)
{
	struct fnode *d = dir->priv;
	mutex_lock(&d->fs->lock);
	struct idx_ctx ic = { index, 0, out };
	int r = dir_scan(d, idx_cb, &ic);
	mutex_unlock(&d->fs->lock);
	return r;
}

static bool free_cb(struct ffs0_dirent *e, u64 off, void *c)
{
	if (!e->ino) {
		*(u64 *)c = off;
		return true;
	}
	return false;
}

static int add_entry(struct fnode *d, const char *name, u32 ino, u8 type)
{
	u64 off = d->vn.size;
	dir_scan(d, free_cb, &off);
	struct ffs0_dirent e;
	memset(&e, 0, sizeof(e));
	e.ino = ino;
	e.type = type;
	e.name_len = (u8)strlen(name);
	memcpy(e.name, name, e.name_len);
	return rw_locked(d, &e, sizeof(e), off, true) == sizeof(e) ? 0 : -E_NOSPC;
}

static int f_create(struct vnode *dir, const char *name, int type, struct vnode **out)
{
	struct fnode *d = dir->priv;
	struct ffs0 *fs = d->fs;
	if (strlen(name) > FFS0_NAME_MAX)
		return -E_NAMETOOLONG;
	mutex_lock(&fs->lock);
	struct find_ctx fc = { .name = name };
	if (dir_scan(d, find_cb, &fc)) {
		mutex_unlock(&fs->lock);
		return -E_EXIST;
	}
	u8 t = type == VT_DIR ? FFS0_T_DIR : FFS0_T_FILE;
	u32 ino = alloc_inode(fs, t);
	if (!ino) {
		mutex_unlock(&fs->lock);
		return -E_NOSPC;
	}
	int r = add_entry(d, name, ino, t);
	if (r == 0) {
		write_super(fs);
		*out = get_vnode(fs, ino);
		(*out)->mnt = dir->mnt;
	}
	mutex_unlock(&fs->lock);
	return r;
}

static bool nonempty_cb(struct ffs0_dirent *e, u64 off, void *c) { return e->ino != 0; }

static int f_unlink(struct vnode *dir, const char *name)
{
	struct fnode *d = dir->priv;
	struct ffs0 *fs = d->fs;
	mutex_lock(&fs->lock);
	struct find_ctx fc = { .name = name };
	if (!dir_scan(d, find_cb, &fc)) {
		mutex_unlock(&fs->lock);
		return -E_NOENT;
	}
	struct ffs0_inode in;
	inode_rw(fs, fc.ino, &in, false);
	if (in.type == FFS0_T_DIR) {
		struct vnode *cv = get_vnode(fs, fc.ino);
		bool nonempty = dir_scan(cv->priv, nonempty_cb, NULL);
		mutex_unlock(&fs->lock);
		vnode_put(cv);
		if (nonempty)
			return -E_NOTEMPTY;
		mutex_lock(&fs->lock);
	}
	struct ffs0_dirent z;
	memset(&z, 0, sizeof(z));
	rw_locked(d, &z, sizeof(z), fc.off, true);
	in.links = 0;
	inode_rw(fs, fc.ino, &in, true);
	/* Free now unless someone holds it open (then on last release). */
	bool open = false;
	list_for_each(it, &fs->vnodes) if (container_of(it, struct fnode, node)->ino == fc.ino) open = true;
	if (!open) {
		free_all_blocks(fs, &in);
		in.type = 0;
		inode_rw(fs, fc.ino, &in, true);
		free_inode_num(fs, fc.ino);
	}
	write_super(fs);
	mutex_unlock(&fs->lock);
	return 0;
}

static int f_rename(struct vnode *odir, const char *oname, struct vnode *ndir, const char *nname)
{
	struct fnode *od = odir->priv, *nd = ndir->priv;
	struct ffs0 *fs = od->fs;
	if (strlen(nname) > FFS0_NAME_MAX)
		return -E_NAMETOOLONG;
	mutex_lock(&fs->lock);
	struct find_ctx fc = { .name = oname };
	if (!dir_scan(od, find_cb, &fc)) {
		mutex_unlock(&fs->lock);
		return -E_NOENT;
	}
	struct find_ctx ex = { .name = nname };
	if (dir_scan(nd, find_cb, &ex)) {
		mutex_unlock(&fs->lock);
		return -E_EXIST;
	}
	struct ffs0_dirent z;
	memset(&z, 0, sizeof(z));
	rw_locked(od, &z, sizeof(z), fc.off, true);
	int r = add_entry(nd, nname, fc.ino, fc.type);
	mutex_unlock(&fs->lock);
	return r;
}

static int f_truncate(struct vnode *vn, u64 len)
{
	struct fnode *f = vn->priv;
	mutex_lock(&f->fs->lock);
	struct ffs0_inode in;
	inode_rw(f->fs, f->ino, &in, false);
	if (len == 0) {
		free_all_blocks(f->fs, &in);
		in.size = 0;
	} else if (len > in.size) {
		in.size = len; /* grows sparsely */
	} else {
		in.size = len; /* shrink: tail blocks stay allocated (simplification) */
	}
	inode_rw(f->fs, f->ino, &in, true);
	vn->size = in.size;
	write_super(f->fs);
	mutex_unlock(&f->fs->lock);
	return 0;
}

static const struct vnode_ops ffs0_ops = {
	.lookup = f_lookup, .read = f_read, .write = f_write, .readdir = f_readdir,
	.create = f_create, .unlink = f_unlink, .rename = f_rename, .truncate = f_truncate,
	.release = f_release,
};

static int ffs0_mount(const char *src, struct vnode **root, void **fsdata)
{
	struct blkdev *dev = blk_get(src);
	if (!dev)
		return -E_NOENT;
	struct buf *b = bread(dev, 0);
	struct ffs0_super sb;
	memcpy(&sb, b->data, sizeof(sb));
	brelse(b);
	if (sb.magic != FFS0_MAGIC || sb.block_size != FFS0_BLOCK)
		return -E_INVAL;
	if (sb.total_blocks > dev->sectors / SECTORS_PER_BLOCK)
		return -E_INVAL;
	struct ffs0 *fs = kzalloc(sizeof(*fs));
	fs->dev = dev;
	fs->sb = sb;
	fs->alloc_hint = sb.data_start;
	mutex_init(&fs->lock);
	list_init(&fs->vnodes);
	fs->sb.mount_count++;
	fs->sb.last_mount = (u64)time_unix();
	if (!sb.clean)
		KLOG("ffs0", "warning: filesystem was not cleanly unmounted (no fsck yet)");
	fs->sb.clean = 0;
	write_super(fs);
	*root = get_vnode(fs, sb.root_ino);
	*fsdata = fs;
	KLOG("ffs0", "'%s' on %s: %lu blocks (%lu free), %u inodes (%u free), mount #%u",
	     sb.label, src, sb.total_blocks, sb.free_blocks, sb.inode_count, sb.free_inodes,
	     fs->sb.mount_count);
	return *root ? 0 : -E_IO;
}

static void ffs0_sync(void *fsdata)
{
	struct ffs0 *fs = fsdata;
	mutex_lock(&fs->lock);
	write_super(fs);
	mutex_unlock(&fs->lock);
	bsync(fs->dev);
}

static int ffs0_statfs(void *fsdata, u64 *total, u64 *freeb, u32 *bs)
{
	struct ffs0 *fs = fsdata;
	*total = fs->sb.total_blocks;
	*freeb = fs->sb.free_blocks;
	*bs = FFS0_BLOCK;
	return 0;
}

const struct fs_type ffs0_type = { .name = "ffs0", .mount = ffs0_mount, .sync = ffs0_sync,
				   .statfs = ffs0_statfs };
