/* virtio-blk driver + the generic block device registry. */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "blk.h"
#include "kernel.h"
#include "mm.h"
#include "virtio.h"

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4
#define VIRTIO_BLK_F_FLUSH (1ULL << 9)

#define MAX_BLK 4
static struct blkdev *blkdevs[MAX_BLK];
static u32 nblk;

int blk_register(struct blkdev *d)
{
	if (nblk >= MAX_BLK)
		return -E_NOSPC;
	d->id = nblk;
	blkdevs[nblk++] = d;
	KLOG("blk", "%s: %lu MiB (%lu sectors of %u B)", d->name, d->sectors * d->sector_size >> 20,
	     d->sectors, d->sector_size);
	return 0;
}

struct blkdev *blk_get(const char *name)
{
	for (u32 i = 0; i < nblk; i++)
		if (!strcmp(blkdevs[i]->name, name))
			return blkdevs[i];
	return NULL;
}

int blk_root_device_present(void) { return blk_get("vda") != NULL; }

int blk_submit(struct blkdev *d, struct blk_req *r)
{
	r->done = false;
	r->status = 0;
	r->submit_ns = time_ns();
	return d->submit(d, r);
}

int blk_rw(struct blkdev *d, u64 sector, u32 count, paddr_t buf, bool write)
{
	struct blk_req r = { .sector = sector, .count = count, .buf = buf, .write = write };
	wq_init(&r.wait);
	int e = blk_submit(d, &r);
	if (e < 0)
		return e;
	u64 f = irq_save();
	while (!r.done)
		wq_wait(&r.wait, 0);
	irq_restore(f);
	return r.status;
}

int blk_flush(struct blkdev *d)
{
	if (!d->write_cache)
		return 0; /* write-through: every completed write is already durable */
	struct blk_req r = { .flush = true };
	wq_init(&r.wait);
	int e = blk_submit(d, &r);
	if (e < 0)
		return e;
	u64 f = irq_save();
	while (!r.done)
		wq_wait(&r.wait, 0);
	irq_restore(f);
	d->flushes++;
	return r.status;
}

void blk_flush_all(void)
{
	for (u32 i = 0; i < nblk; i++)
		blk_flush(blkdevs[i]);
}

/* ---- virtio-blk ---- */
struct vblk {
	struct virtio_dev v;
	struct blkdev dev;
	paddr_t hdr_pool;	/* per-descriptor-head request headers + status bytes */
	u8 *hdr_virt;
};

struct PACKED vblk_hdr {
	u32 type;
	u32 reserved;
	u64 sector;
};

static void vblk_irq(struct trap_frame *tf, void *ctx)
{
	struct vblk *b = ctx;
	struct blk_req *r;
	u32 len;
	while ((r = virtq_pop(b->v.vq[0], &len))) {
		u8 status = *(volatile u8 *)(b->hdr_virt + (u64)(uintptr_t)r->ctx * 32 + 16);
		r->status = status == 0 ? 0 : -E_IO;
		r->done = true;
		b->dev.inflight--;
		u64 lat = time_ns() - r->submit_ns;
		b->dev.busy_ns += lat;
		if (r->flush)
			;
		else if (r->write)
			b->dev.write_ns += lat;
		else
			b->dev.read_ns += lat;
		if (r->complete)
			r->complete(r);
		else
			wq_wake_all(&r->wait);
	}
}

static int vblk_submit(struct blkdev *d, struct blk_req *r)
{
	struct vblk *b = d->priv;
	struct virtq *q = b->v.vq[0];
	/* Pick a header slot = the head descriptor index we are about to use. */
	u64 f = irq_save();
	u16 slot = q->free_head;
	struct vblk_hdr *h = (struct vblk_hdr *)(b->hdr_virt + (u64)slot * 32);
	h->type = r->flush ? VIRTIO_BLK_T_FLUSH : r->write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
	h->reserved = 0;
	h->sector = r->flush ? 0 : r->sector;
	*(b->hdr_virt + (u64)slot * 32 + 16) = 0xFF;
	paddr_t hp = b->hdr_pool + (u64)slot * 32;
	r->ctx = (void *)(uintptr_t)slot;
	int head;
	if (r->flush) { /* header + status only */
		paddr_t addrs[2] = { hp, hp + 16 };
		u32 lens[2] = { 16, 1 };
		u16 flags[2] = { 0, VIRTQ_DESC_F_WRITE };
		head = virtq_add(q, addrs, lens, flags, 2, r);
	} else {
		paddr_t addrs[3] = { hp, r->buf, hp + 16 };
		u32 lens[3] = { 16, r->count * 512, 1 };
		u16 flags[3] = { 0, r->write ? 0 : VIRTQ_DESC_F_WRITE, VIRTQ_DESC_F_WRITE };
		head = virtq_add(q, addrs, lens, flags, 3, r);
	}
	if (head < 0) {
		irq_restore(f);
		return head;
	}
	d->inflight++;
	if (d->inflight > d->max_inflight)
		d->max_inflight = d->inflight;
	if (r->flush) {
		/* counted by blk_flush */
	} else if (r->write) {
		d->writes++;
		d->write_bytes += (u64)r->count * 512;
	} else {
		d->reads++;
		d->read_bytes += (u64)r->count * 512;
	}
	virtq_kick(q);
	irq_restore(f);
	return 0;
}

int virtio_blk_probe(struct pci_dev *d)
{
	struct vblk *b = kzalloc(sizeof(*b));
	/* Negotiating FLUSH gives the device a write cache: writes complete
	 * without a host flush each, and bsync() issues explicit flushes.
	 * Without it the device must act write-through, i.e. QEMU flushed the
	 * image file after every 4 KiB write (3.2 ms each, F-123). */
	int r = virtio_init(&b->v, d, VIRTIO_BLK_F_FLUSH);
	if (r < 0) {
		KLOG("vblk", "init failed (%d)", r);
		return r;
	}
	int vec = irq_alloc_vector();
	if (vec < 0 || pci_msix_set(d, 0, (u8)vec) < 0)
		return -E_NOSYS;
	irq_register((u8)vec, vblk_irq, b);
	pci_msix_enable(d);
	if (virtio_setup_queue(&b->v, 0, 128, 0) < 0)
		return -E_IO;
	b->hdr_pool = pmm_alloc_frames(1); /* 128 slots x 32 B */
	b->hdr_virt = phys_to_virt(b->hdr_pool);
	virtio_driver_ok(&b->v);
	b->dev.write_cache = (b->v.features & VIRTIO_BLK_F_FLUSH) != 0;
	u64 cap = *(volatile u64 *)b->v.devcfg;
	b->dev.sectors = cap;
	b->dev.sector_size = 512;
	b->dev.submit = vblk_submit;
	b->dev.priv = b;
	snprintf(b->dev.name, sizeof(b->dev.name), "vd%c", 'a' + (int)nblk);
	return blk_register(&b->dev);
}

struct pbuf;
void pb_printf(struct pbuf *p, const char *fmt, ...);
void procfs_register(const char *name, void (*gen)(void *p));
static void gen_blk(void *pb)
{
	for (u32 i = 0; i < nblk; i++) {
		struct blkdev *d = blkdevs[i];
		pb_printf(pb, "%s sectors=%lu reads=%lu writes=%lu read_bytes=%lu write_bytes=%lu busy_ms=%lu max_inflight=%u"
			      " read_avg_us=%lu write_avg_us=%lu write_cache=%d flushes=%lu\n",
			  d->name, d->sectors, d->reads, d->writes, d->read_bytes, d->write_bytes,
			  d->busy_ns / 1000000, d->max_inflight, d->reads ? d->read_ns / d->reads / 1000 : 0,
			  d->writes ? d->write_ns / d->writes / 1000 : 0, d->write_cache, d->flushes);
	}
}

void blk_init_devices(void)
{
	procfs_register("blk", (void (*)(void *))gen_blk);
	bcache_init(2048); /* 8 MiB default; `cachectl capacity N` changes it */
}
