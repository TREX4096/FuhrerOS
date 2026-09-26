/* Virtual filesystem layer (NEW_EXPLANATION §21).
 *
 *   /        FFS0 on virtio-blk (falls back to a RAM filesystem)
 *   /initrd  the boot module (read-only ustar)
 *   /dev     devices      /proc  kernel state      /tmp  RAM filesystem
 */
#ifndef FUHRER_VFS_H
#define FUHRER_VFS_H

#include "sched.h"
#include "types.h"

#include "uapi/fuhrer.h"

#define PATH_MAX_VFS 256

struct vnode;

struct vnode_ops {
	int (*lookup)(struct vnode *dir, const char *name, struct vnode **out);
	ssize_t (*read)(struct vnode *vn, void *buf, u64 len, u64 off, int flags);
	ssize_t (*write)(struct vnode *vn, const void *buf, u64 len, u64 off, int flags);
	int (*readdir)(struct vnode *dir, u64 index, struct dirent *out);
	int (*create)(struct vnode *dir, const char *name, int type, struct vnode **out);
	int (*unlink)(struct vnode *dir, const char *name);
	int (*rename)(struct vnode *odir, const char *oname, struct vnode *ndir, const char *nname);
	int (*truncate)(struct vnode *vn, u64 len);
	void (*release)(struct vnode *vn);		/* last reference dropped */
	int (*ioctl)(struct vnode *vn, u64 req, u64 arg);
	void (*close)(struct vnode *vn, int flags);	/* an open file was closed */
	int (*poll)(struct vnode *vn, int events);	/* 1=readable 2=writable */
	void (*sync)(struct vnode *vn);
};

struct mount;

struct vnode {
	int type;
	u32 ino;
	u64 size;
	u64 mtime;
	u32 nlink;
	int refcnt;
	const struct vnode_ops *ops;
	void *priv;
	struct mount *mnt;
	u32 dev;
};

struct file {
	struct vnode *vn;
	u64 off;
	int flags;
	int refcnt;
};

struct fs_type {
	const char *name;
	int (*mount)(const char *source, struct vnode **root, void **fsdata);
	void (*sync)(void *fsdata);
	int (*statfs)(void *fsdata, u64 *total_blocks, u64 *free_blocks, u32 *block_size);
};

struct mount {
	char path[64];
	struct vnode *root;
	const struct fs_type *fs;
	void *fsdata;
	u32 dev;
};

void vfs_init(void);
void vfs_register_fs(const struct fs_type *fs);
int vfs_mount(const char *fstype, const char *source, const char *path);
struct mount *vfs_mounts(u32 *count);

void vnode_ref(struct vnode *vn);
void vnode_put(struct vnode *vn);

/* Path resolution relative to cwd (absolute paths ignore it). */
int vfs_resolve(const char *cwd, const char *path, struct vnode **out);
int vfs_normalize(const char *cwd, const char *path, char *out, u64 outlen);

int vfs_open(const char *cwd, const char *path, int flags, struct file **out);
struct file *file_from_vnode(struct vnode *vn, int flags);
void file_ref(struct file *f);
void file_close(struct file *f);
ssize_t file_read(struct file *f, void *buf, u64 len);
ssize_t file_write(struct file *f, const void *buf, u64 len);
int file_readdir(struct file *f, u64 index, struct dirent *out);
int vfs_stat(const char *cwd, const char *path, struct stat *st);
void vnode_stat(struct vnode *vn, struct stat *st);
int vfs_mkdir(const char *cwd, const char *path);
int vfs_unlink(const char *cwd, const char *path);
int vfs_rename(const char *cwd, const char *from, const char *to);
int vfs_truncate(const char *cwd, const char *path, u64 len);
void vfs_sync_all(void);

/* Read a whole file into a kmalloc'ed buffer (for the ELF loader). */
int vfs_read_all(const char *path, void **buf, u64 *len);

/* Filesystem types */
extern const struct fs_type tarfs_type, ramfs_type, devfs_type, procfs_type, ffs0_type;

/* devfs: register a character/block device node */
int devfs_register(const char *name, int type, const struct vnode_ops *ops, void *priv);

#endif
