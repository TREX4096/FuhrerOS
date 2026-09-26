/* In-memory filesystem used three ways:
 *   ramfs  - writable (/tmp, and / when no disk is present)
 *   tarfs  - the ustar initrd, read-only, file data points into the module
 *   devfs  - directory of device nodes registered by drivers
 * Nodes live forever (no eviction); unlink only detaches them. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "list.h"
#include "mm.h"
#include "vfs.h"

struct mnode {
	struct vnode vn;
	char name[NAME_MAX + 1];
	struct mnode *parent;
	struct list_node children;
	struct list_node sibling;
	u8 *data;
	u64 cap;
	bool readonly;
	bool external;		/* data not owned (points into the initrd) */
};

static u32 next_ino = 1;
static const struct vnode_ops memfs_ops;

static struct mnode *mnode_new(const char *name, int type, struct mnode *parent)
{
	struct mnode *m = kzalloc(sizeof(*m));
	if (!m)
		return NULL;
	strlcpy(m->name, name, sizeof(m->name));
	m->vn.type = type;
	m->vn.ino = next_ino++;
	m->vn.ops = &memfs_ops;
	m->vn.priv = m;
	m->vn.refcnt = 1; /* the tree holds one reference */
	m->vn.nlink = 1;
	m->vn.mtime = (u64)time_unix();
	list_init(&m->children);
	list_init(&m->sibling);
	if (parent) {
		m->parent = parent;
		m->readonly = parent->readonly;
		m->vn.mnt = parent->vn.mnt;
		list_push_back(&parent->children, &m->sibling);
	}
	return m;
}

static struct mnode *child(struct mnode *dir, const char *name)
{
	list_for_each(it, &dir->children) {
		struct mnode *c = container_of(it, struct mnode, sibling);
		if (!strcmp(c->name, name))
			return c;
	}
	return NULL;
}

static int m_lookup(struct vnode *dir, const char *name, struct vnode **out)
{
	struct mnode *c = child(dir->priv, name);
	if (!c)
		return -E_NOENT;
	vnode_ref(&c->vn);
	*out = &c->vn;
	return 0;
}

static ssize_t m_read(struct vnode *vn, void *buf, u64 len, u64 off, int flags)
{
	struct mnode *m = vn->priv;
	if (off >= vn->size)
		return 0;
	u64 n = MIN(len, vn->size - off);
	memcpy(buf, m->data + off, n);
	return (ssize_t)n;
}

static int grow(struct mnode *m, u64 need)
{
	if (need <= m->cap && !m->external)
		return 0;
	u64 cap = MAX(need, m->cap * 2);
	cap = MAX(cap, 256);
	u8 *nd = kmalloc(cap);
	if (!nd)
		return -E_NOMEM;
	if (m->vn.size)
		memcpy(nd, m->data, m->vn.size);
	if (!m->external)
		kfree(m->data);
	m->external = false;
	m->data = nd;
	m->cap = cap;
	return 0;
}

static ssize_t m_write(struct vnode *vn, const void *buf, u64 len, u64 off, int flags)
{
	struct mnode *m = vn->priv;
	if (m->readonly)
		return -E_PERM;
	if (grow(m, off + len) < 0)
		return -E_NOSPC;
	if (off > vn->size)
		memset(m->data + vn->size, 0, off - vn->size);
	memcpy(m->data + off, buf, len);
	if (off + len > vn->size)
		vn->size = off + len;
	vn->mtime = (u64)time_unix();
	return (ssize_t)len;
}

static int m_truncate(struct vnode *vn, u64 len)
{
	struct mnode *m = vn->priv;
	if (m->readonly)
		return -E_PERM;
	if (len > vn->size) {
		if (grow(m, len) < 0)
			return -E_NOSPC;
		memset(m->data + vn->size, 0, len - vn->size);
	}
	vn->size = len;
	return 0;
}

static int m_readdir(struct vnode *dir, u64 index, struct dirent *out)
{
	struct mnode *d = dir->priv;
	u64 i = 0;
	list_for_each(it, &d->children) {
		if (i++ == index) {
			struct mnode *c = container_of(it, struct mnode, sibling);
			out->ino = c->vn.ino;
			out->type = (u8)c->vn.type;
			strlcpy(out->name, c->name, sizeof(out->name));
			return 1;
		}
	}
	return 0;
}

static int m_create(struct vnode *dir, const char *name, int type, struct vnode **out)
{
	struct mnode *d = dir->priv;
	if (d->readonly)
		return -E_PERM;
	if (child(d, name))
		return -E_EXIST;
	struct mnode *m = mnode_new(name, type, d);
	if (!m)
		return -E_NOMEM;
	vnode_ref(&m->vn);
	*out = &m->vn;
	return 0;
}

static int m_unlink(struct vnode *dir, const char *name)
{
	struct mnode *d = dir->priv, *c = child(d, name);
	if (d->readonly)
		return -E_PERM;
	if (!c)
		return -E_NOENT;
	if (c->vn.type == VT_DIR && !list_empty(&c->children))
		return -E_NOTEMPTY;
	list_remove(&c->sibling);
	c->parent = NULL;
	vnode_put(&c->vn); /* drop the tree's reference */
	return 0;
}

static int m_rename(struct vnode *odir, const char *oname, struct vnode *ndir, const char *nname)
{
	struct mnode *od = odir->priv, *nd = ndir->priv, *c = child(od, oname);
	if (od->readonly || nd->readonly)
		return -E_PERM;
	if (!c)
		return -E_NOENT;
	struct mnode *ex = child(nd, nname);
	if (ex == c)
		return 0;
	if (ex) {
		if (ex->vn.type == VT_DIR && !list_empty(&ex->children))
			return -E_NOTEMPTY;
		list_remove(&ex->sibling);
		vnode_put(&ex->vn);
	}
	list_remove(&c->sibling);
	strlcpy(c->name, nname, sizeof(c->name));
	c->parent = nd;
	list_push_back(&nd->children, &c->sibling);
	return 0;
}

static void m_release(struct vnode *vn)
{
	struct mnode *m = vn->priv;
	if (m->parent)
		return; /* still linked (should not happen) */
	if (!m->external)
		kfree(m->data);
	kfree(m);
}

static const struct vnode_ops memfs_ops = {
	.lookup = m_lookup,
	.read = m_read,
	.write = m_write,
	.readdir = m_readdir,
	.create = m_create,
	.unlink = m_unlink,
	.rename = m_rename,
	.truncate = m_truncate,
	.release = m_release,
};

/* ---- ramfs ---- */
static int ramfs_mount(const char *src, struct vnode **root, void **fsdata)
{
	struct mnode *r = mnode_new("/", VT_DIR, NULL);
	if (!r)
		return -E_NOMEM;
	*root = &r->vn;
	*fsdata = r;
	return 0;
}
const struct fs_type ramfs_type = { .name = "ramfs", .mount = ramfs_mount };

/* ---- tarfs (ustar) ---- */
struct PACKED ustar {
	char name[100], mode[8], uid[8], gid[8], size[12], mtime[12], chksum[8];
	char type;
	char linkname[100], magic[6], version[2], uname[32], gname[32], devmajor[8],
		devminor[8], prefix[155];
};

static u64 octal(const char *s, int n)
{
	u64 v = 0;
	for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
		v = v * 8 + (u64)(s[i] - '0');
	return v;
}

/* Find or create each directory along path; returns the final parent. */
static struct mnode *mkpath(struct mnode *root, char *path, char **leaf)
{
	struct mnode *cur = root;
	char *p = path;
	for (;;) {
		while (*p == '/')
			p++;
		char *e = strchr(p, '/');
		if (!e || !e[1]) {
			if (e)
				*e = 0;
			*leaf = p;
			return cur;
		}
		*e = 0;
		struct mnode *c = child(cur, p);
		if (!c)
			c = mnode_new(p, VT_DIR, cur);
		cur = c;
		p = e + 1;
	}
}

static int tarfs_mount(const char *src, struct vnode **root, void **fsdata)
{
	struct boot_module *mod = NULL;
	for (u32 i = 0; i < boot.module_count; i++)
		if (!strcmp(boot.modules[i].cmdline, src))
			mod = &boot.modules[i];
	if (!mod)
		return -E_NOENT;
	struct mnode *r = mnode_new("/", VT_DIR, NULL);
	u8 *p = mod->virt, *end = p + mod->size;
	u32 files = 0;
	while (p + 512 <= end) {
		struct ustar *h = (struct ustar *)p;
		if (!h->name[0])
			break;
		if (memcmp(h->magic, "ustar", 5))
			break;
		u64 size = octal(h->size, 12);
		char path[256];
		if (h->prefix[0])
			snprintf(path, sizeof(path), "%.155s/%.100s", h->prefix, h->name);
		else
			snprintf(path, sizeof(path), "%.100s", h->name);
		char *leaf;
		char *pp = path;
		if (pp[0] == '.' && pp[1] == '/')
			pp += 2;
		if (*pp && strcmp(pp, ".")) {
			struct mnode *dir = mkpath(r, pp, &leaf);
			if (*leaf && !child(dir, leaf)) {
				int type = (h->type == '5') ? VT_DIR : VT_FILE;
				struct mnode *m = mnode_new(leaf, type, dir);
				if (type == VT_FILE) {
					m->data = p + 512;
					m->vn.size = size;
					m->external = true;
					files++;
				}
				m->vn.mtime = octal(h->mtime, 12);
			}
		}
		p += 512 + ALIGN_UP(size, 512);
	}
	/* Everything in the initrd is read-only. */
	r->readonly = true;
	struct list_node *stack[64];
	int sp = 0;
	stack[sp++] = &r->children;
	while (sp) {
		struct list_node *h = stack[--sp];
		list_for_each(it, h) {
			struct mnode *c = container_of(it, struct mnode, sibling);
			c->readonly = true;
			if (c->vn.type == VT_DIR && sp < 64)
				stack[sp++] = &c->children;
		}
	}
	*root = &r->vn;
	*fsdata = r;
	KLOG("tarfs", "initrd: %u files, %lu KiB", files, mod->size / 1024);
	return 0;
}
const struct fs_type tarfs_type = { .name = "tarfs", .mount = tarfs_mount };

/* ---- devfs ---- */
static struct mnode *devroot;

static int devfs_mount(const char *src, struct vnode **root, void **fsdata)
{
	if (!devroot)
		devroot = mnode_new("/", VT_DIR, NULL);
	devroot->readonly = true;
	*root = &devroot->vn;
	*fsdata = devroot;
	return 0;
}
const struct fs_type devfs_type = { .name = "devfs", .mount = devfs_mount };

/* Device vnodes get the driver's ops; the node itself only provides naming. */
int devfs_register(const char *name, int type, const struct vnode_ops *ops, void *priv)
{
	if (!devroot)
		devroot = mnode_new("/", VT_DIR, NULL);
	struct mnode *m = mnode_new(name, type, devroot);
	if (!m)
		return -E_NOMEM;
	m->vn.ops = ops;
	m->vn.priv = priv;
	m->vn.refcnt = 1 << 20; /* devices are never released */
	return 0;
}
