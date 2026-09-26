/* VFS core: mount table, path normalisation/resolution, open files. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"
#include "vfs.h"

#define MAX_MOUNTS 16
#define MAX_FS 8

/* Filesystems that may not be built yet are referenced weakly. */
extern const struct fs_type ffs0_type __attribute__((weak));

static struct mount mounts[MAX_MOUNTS];
static u32 nmounts;
static const struct fs_type *fstypes[MAX_FS];
static u32 nfs;
static struct mutex vfs_lock;

void vfs_init(void)
{
	mutex_init(&vfs_lock);
	vfs_register_fs(&ramfs_type);
	vfs_register_fs(&tarfs_type);
	vfs_register_fs(&devfs_type);
	vfs_register_fs(&procfs_type);
	if (&ffs0_type)
		vfs_register_fs(&ffs0_type);
}

void vfs_register_fs(const struct fs_type *fs)
{
	if (nfs < MAX_FS)
		fstypes[nfs++] = fs;
}

struct mount *vfs_mounts(u32 *count)
{
	*count = nmounts;
	return mounts;
}

int vfs_mount(const char *fstype, const char *source, const char *path)
{
	const struct fs_type *fs = NULL;
	for (u32 i = 0; i < nfs; i++)
		if (!strcmp(fstypes[i]->name, fstype))
			fs = fstypes[i];
	if (!fs || nmounts >= MAX_MOUNTS)
		return -E_INVAL;
	struct mount *m = &mounts[nmounts];
	int r = fs->mount(source, &m->root, &m->fsdata);
	if (r < 0)
		return r;
	strlcpy(m->path, path, sizeof(m->path));
	m->fs = fs;
	m->dev = nmounts + 1;
	m->root->mnt = m;
	nmounts++;
	KLOG("vfs", "mounted %s (%s) on %s", source ? source : "-", fstype, path);
	return 0;
}

void vnode_ref(struct vnode *vn)
{
	u64 f = irq_save();
	vn->refcnt++;
	irq_restore(f);
}

void vnode_put(struct vnode *vn)
{
	if (!vn)
		return;
	u64 f = irq_save();
	bool last = --vn->refcnt == 0;
	irq_restore(f);
	if (last && vn->ops->release)
		vn->ops->release(vn);
}

/* Build an absolute, normalised path ("." and ".." removed). */
int vfs_normalize(const char *cwd, const char *path, char *out, u64 outlen)
{
	char tmp[512];
	if (path[0] == '/')
		strlcpy(tmp, path, sizeof(tmp));
	else
		snprintf(tmp, sizeof(tmp), "%s/%s", cwd && cwd[0] ? cwd : "/", path);
	u64 n = 0;
	out[n++] = '/';
	char *p = tmp;
	while (*p) {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		char *e = p;
		while (*e && *e != '/')
			e++;
		u64 len = (u64)(e - p);
		if (len == 1 && p[0] == '.') {
			/* skip */
		} else if (len == 2 && p[0] == '.' && p[1] == '.') {
			if (n > 1) {
				n--;
				while (n > 1 && out[n - 1] != '/')
					n--;
			}
		} else {
			if (len > NAME_MAX)
				return -E_NAMETOOLONG;
			if (n > 1)
				out[n++] = '/';
			if (n + len + 1 >= outlen)
				return -E_NAMETOOLONG;
			memcpy(out + n, p, len);
			n += len;
		}
		p = e;
	}
	if (n > 1 && out[n - 1] == '/')
		n--;
	out[n] = 0;
	return 0;
}

/* Longest mount prefix of an absolute normalised path. */
static struct mount *find_mount(const char *abs, const char **rest)
{
	struct mount *best = NULL;
	u64 bl = 0;
	for (u32 i = 0; i < nmounts; i++) {
		u64 l = strlen(mounts[i].path);
		bool match = (l == 1 && abs[0] == '/') ||
			     (!strncmp(abs, mounts[i].path, l) && (abs[l] == '/' || abs[l] == 0));
		if (match && l >= bl) {
			best = &mounts[i];
			bl = l;
		}
	}
	if (best && rest)
		*rest = abs + (bl == 1 ? 1 : bl);
	return best;
}

static int walk(const char *abs, struct vnode **out, struct vnode **parent, char *last)
{
	const char *rest;
	struct mount *m = find_mount(abs, &rest);
	if (!m)
		return -E_NOENT;
	struct vnode *cur = m->root, *par = NULL;
	vnode_ref(cur);
	char name[NAME_MAX + 1] = "";
	while (*rest) {
		while (*rest == '/')
			rest++;
		if (!*rest)
			break;
		const char *e = rest;
		while (*e && *e != '/')
			e++;
		u64 len = (u64)(e - rest);
		memcpy(name, rest, len);
		name[len] = 0;
		rest = e;
		if (cur->type != VT_DIR) {
			vnode_put(cur);
			vnode_put(par);
			return -E_NOTDIR;
		}
		struct vnode *next = NULL;
		int r = cur->ops->lookup ? cur->ops->lookup(cur, name, &next) : -E_NOENT;
		if (r < 0) {
			bool is_last = !*rest || (rest[0] == '/' && !rest[1]);
			if (is_last && parent) {
				/* caller wants the parent of a missing final component */
				vnode_put(par);
				*parent = cur;
				if (last)
					strlcpy(last, name, NAME_MAX + 1);
				*out = NULL;
				return r;
			}
			vnode_put(cur);
			vnode_put(par);
			return r;
		}
		vnode_put(par);
		par = cur;
		cur = next;
	}
	if (parent) {
		*parent = par;
		if (last)
			strlcpy(last, name, NAME_MAX + 1);
	} else {
		vnode_put(par);
	}
	*out = cur;
	return 0;
}

int vfs_resolve(const char *cwd, const char *path, struct vnode **out)
{
	char abs[PATH_MAX_VFS];
	int r = vfs_normalize(cwd, path, abs, sizeof(abs));
	if (r < 0)
		return r;
	return walk(abs, out, NULL, NULL);
}

struct file *file_from_vnode(struct vnode *vn, int flags)
{
	struct file *f = kzalloc(sizeof(*f));
	if (!f)
		return NULL;
	f->vn = vn;
	f->flags = flags;
	f->refcnt = 1;
	return f;
}

int vfs_open(const char *cwd, const char *path, int flags, struct file **out)
{
	char abs[PATH_MAX_VFS];
	int r = vfs_normalize(cwd, path, abs, sizeof(abs));
	if (r < 0)
		return r;
	struct vnode *vn = NULL, *parent = NULL;
	char last[NAME_MAX + 1];
	if (flags & O_CREAT) {
		r = walk(abs, &vn, &parent, last);
		if (r == -E_NOENT && parent) {
			if (!parent->ops->create) {
				vnode_put(parent);
				return -E_PERM;
			}
			r = parent->ops->create(parent, last, VT_FILE, &vn);
		}
		vnode_put(parent);
	} else {
		r = walk(abs, &vn, NULL, NULL);
	}
	if (r < 0)
		return r;
	if ((flags & O_DIRECTORY) && vn->type != VT_DIR) {
		vnode_put(vn);
		return -E_NOTDIR;
	}
	if (vn->type == VT_DIR && (flags & O_ACCMODE) != O_RDONLY) {
		vnode_put(vn);
		return -E_ISDIR;
	}
	if ((flags & O_TRUNC) && vn->type == VT_FILE && vn->ops->truncate)
		vn->ops->truncate(vn, 0);
	struct file *f = file_from_vnode(vn, flags);
	if (!f) {
		vnode_put(vn);
		return -E_NOMEM;
	}
	if (flags & O_APPEND)
		f->off = vn->size;
	*out = f;
	return 0;
}

void file_ref(struct file *f)
{
	u64 fl = irq_save();
	f->refcnt++;
	irq_restore(fl);
}

void file_close(struct file *f)
{
	if (!f)
		return;
	u64 fl = irq_save();
	bool last = --f->refcnt == 0;
	irq_restore(fl);
	if (!last)
		return;
	if (f->vn->ops->close)
		f->vn->ops->close(f->vn, f->flags);
	vnode_put(f->vn);
	kfree(f);
}

ssize_t file_read(struct file *f, void *buf, u64 len)
{
	if ((f->flags & O_ACCMODE) == O_WRONLY)
		return -E_BADF;
	if (!f->vn->ops->read)
		return -E_INVAL;
	if (f->vn->type == VT_DIR)
		return -E_ISDIR;
	ssize_t n = f->vn->ops->read(f->vn, buf, len, f->off, f->flags);
	if (n > 0 && (f->vn->type == VT_FILE || f->vn->type == VT_BLOCK))
		f->off += (u64)n;
	return n;
}

ssize_t file_write(struct file *f, const void *buf, u64 len)
{
	if ((f->flags & O_ACCMODE) == O_RDONLY)
		return -E_BADF;
	if (!f->vn->ops->write)
		return -E_INVAL;
	if (f->flags & O_APPEND)
		f->off = f->vn->size;
	ssize_t n = f->vn->ops->write(f->vn, buf, len, f->off, f->flags);
	if (n > 0 && (f->vn->type == VT_FILE || f->vn->type == VT_BLOCK))
		f->off += (u64)n;
	return n;
}

int file_readdir(struct file *f, u64 index, struct dirent *out)
{
	if (f->vn->type != VT_DIR || !f->vn->ops->readdir)
		return -E_NOTDIR;
	return f->vn->ops->readdir(f->vn, index, out);
}

void vnode_stat(struct vnode *vn, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	st->ino = vn->ino;
	st->type = (u32)vn->type;
	st->size = vn->size;
	st->nlink = vn->nlink ? vn->nlink : 1;
	st->blocks = (u32)((vn->size + 4095) / 4096);
	st->mtime = vn->mtime;
	st->dev = vn->mnt ? vn->mnt->dev : vn->dev;
}

int vfs_stat(const char *cwd, const char *path, struct stat *st)
{
	struct vnode *vn;
	int r = vfs_resolve(cwd, path, &vn);
	if (r < 0)
		return r;
	vnode_stat(vn, st);
	vnode_put(vn);
	return 0;
}

static int parent_op(const char *cwd, const char *path, struct vnode **parent, char *last,
		     struct vnode **existing)
{
	char abs[PATH_MAX_VFS];
	int r = vfs_normalize(cwd, path, abs, sizeof(abs));
	if (r < 0)
		return r;
	if (!strcmp(abs, "/"))
		return -E_BUSY;
	struct vnode *vn = NULL;
	*parent = NULL;
	r = walk(abs, &vn, parent, last);
	*existing = vn;
	if (!*parent) {
		vnode_put(vn);
		return r < 0 ? r : -E_NOENT;
	}
	return 0;
}

int vfs_mkdir(const char *cwd, const char *path)
{
	struct vnode *parent, *ex, *nv = NULL;
	char last[NAME_MAX + 1];
	int r = parent_op(cwd, path, &parent, last, &ex);
	if (r < 0)
		return r;
	if (ex) {
		vnode_put(ex);
		vnode_put(parent);
		return -E_EXIST;
	}
	r = parent->ops->create ? parent->ops->create(parent, last, VT_DIR, &nv) : -E_PERM;
	vnode_put(nv);
	vnode_put(parent);
	return r;
}

int vfs_unlink(const char *cwd, const char *path)
{
	struct vnode *parent, *ex;
	char last[NAME_MAX + 1];
	int r = parent_op(cwd, path, &parent, last, &ex);
	if (r < 0)
		return r;
	if (!ex) {
		vnode_put(parent);
		return -E_NOENT;
	}
	if (ex->mnt && ex->mnt->root == ex) {
		vnode_put(ex);
		vnode_put(parent);
		return -E_BUSY; /* mount point */
	}
	vnode_put(ex);
	r = parent->ops->unlink ? parent->ops->unlink(parent, last) : -E_PERM;
	vnode_put(parent);
	return r;
}

int vfs_rename(const char *cwd, const char *from, const char *to)
{
	struct vnode *op, *oex, *np, *nex;
	char olast[NAME_MAX + 1], nlast[NAME_MAX + 1];
	int r = parent_op(cwd, from, &op, olast, &oex);
	if (r < 0)
		return r;
	if (!oex) {
		vnode_put(op);
		return -E_NOENT;
	}
	vnode_put(oex);
	r = parent_op(cwd, to, &np, nlast, &nex);
	if (r < 0) {
		vnode_put(op);
		return r;
	}
	if (nex && nex->type == VT_DIR) {
		/* rename into a directory: keep the name */
		vnode_put(np);
		np = nex;
		nex = NULL;
		strlcpy(nlast, olast, sizeof(nlast));
	}
	vnode_put(nex);
	if (op->mnt != np->mnt)
		r = -E_INVAL; /* cross-filesystem: use cp + rm */
	else
		r = op->ops->rename ? op->ops->rename(op, olast, np, nlast) : -E_PERM;
	vnode_put(op);
	vnode_put(np);
	return r;
}

int vfs_truncate(const char *cwd, const char *path, u64 len)
{
	struct vnode *vn;
	int r = vfs_resolve(cwd, path, &vn);
	if (r < 0)
		return r;
	r = vn->ops->truncate ? vn->ops->truncate(vn, len) : -E_PERM;
	vnode_put(vn);
	return r;
}

void vfs_sync_all(void)
{
	for (u32 i = 0; i < nmounts; i++)
		if (mounts[i].fs->sync)
			mounts[i].fs->sync(mounts[i].fsdata);
}

int vfs_read_all(const char *path, void **buf, u64 *len)
{
	struct file *f;
	int r = vfs_open("/", path, O_RDONLY, &f);
	if (r < 0)
		return r;
	u64 size = f->vn->size;
	void *b = kmalloc(size ? size : 1);
	if (!b) {
		file_close(f);
		return -E_NOMEM;
	}
	u64 got = 0;
	while (got < size) {
		ssize_t n = file_read(f, (u8 *)b + got, size - got);
		if (n <= 0)
			break;
		got += (u64)n;
	}
	file_close(f);
	if (got != size) {
		kfree(b);
		return -E_IO;
	}
	*buf = b;
	*len = size;
	return 0;
}
