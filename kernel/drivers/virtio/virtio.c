/* virtio-pci modern transport (virtio 1.2 spec §4.1) and split virtqueues. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"
#include "virtio.h"

/* common configuration layout */
#define C_DFSEL 0
#define C_DF 4
#define C_GFSEL 8
#define C_GF 12
#define C_MSIX 16
#define C_NUMQ 18
#define C_STATUS 20
#define C_QSEL 22
#define C_QSIZE 24
#define C_QMSIX 26
#define C_QENABLE 28
#define C_QNOTIFY 30
#define C_QDESC 32
#define C_QDRIVER 40
#define C_QDEVICE 48

#define S_ACK 1
#define S_DRIVER 2
#define S_DRIVER_OK 4
#define S_FEATURES_OK 8
#define F_VERSION_1 (1ULL << 32)

#define W8(v, o, x) (*(volatile u8 *)((v)->common + (o)) = (x))
#define W16(v, o, x) (*(volatile u16 *)((v)->common + (o)) = (x))
#define W32(v, o, x) (*(volatile u32 *)((v)->common + (o)) = (x))
#define W64(v, o, x) (W32(v, o, (u32)(x)), W32(v, (o) + 4, (u32)((u64)(x) >> 32)))
#define R8(v, o) (*(volatile u8 *)((v)->common + (o)))
#define R16(v, o) (*(volatile u16 *)((v)->common + (o)))
#define R32(v, o) (*(volatile u32 *)((v)->common + (o)))

static void *bar_map(struct pci_dev *d, u8 bar, u32 off, u32 len)
{
	static void *mapped[64][6];
	static struct pci_dev *owners[64];
	int slot = -1;
	for (int i = 0; i < 64; i++) {
		if (owners[i] == d) { slot = i; break; }
		if (!owners[i]) { owners[i] = d; slot = i; break; }
	}
	if (!mapped[slot][bar])
		mapped[slot][bar] = ioremap(d->bar[bar], d->bar_size[bar] ? d->bar_size[bar] : 0x4000);
	(void)len;
	return (u8 *)mapped[slot][bar] + off;
}

int virtio_init(struct virtio_dev *v, struct pci_dev *d, u64 wanted)
{
	memset(v, 0, sizeof(*v));
	v->pci = d;
	pci_enable_bus_master(d);
	for (u8 cap = pci_find_cap(d, 0x09, 0); cap; cap = pci_find_cap(d, 0x09, cap)) {
		u8 type = pci_read8(d, cap + 3), bar = pci_read8(d, cap + 4);
		u32 off = pci_read32(d, cap + 8), len = pci_read32(d, cap + 12);
		if (bar > 5 || !d->bar[bar])
			continue;
		switch (type) {
		case 1: v->common = bar_map(d, bar, off, len); break;
		case 2:
			v->notify_base = bar_map(d, bar, off, len);
			v->notify_mult = pci_read32(d, cap + 16);
			break;
		case 3: v->isr = bar_map(d, bar, off, len); break;
		case 4: v->devcfg = bar_map(d, bar, off, len); break;
		}
	}
	if (!v->common || !v->notify_base)
		return -E_NOSYS;
	W8(v, C_STATUS, 0); /* reset */
	while (R8(v, C_STATUS))
		cpu_pause();
	W8(v, C_STATUS, S_ACK);
	W8(v, C_STATUS, S_ACK | S_DRIVER);
	W32(v, C_DFSEL, 0);
	u64 feat = R32(v, C_DF);
	W32(v, C_DFSEL, 1);
	feat |= (u64)R32(v, C_DF) << 32;
	if (!(feat & F_VERSION_1))
		return -E_NOSYS;
	v->features = feat & (wanted | F_VERSION_1);
	W32(v, C_GFSEL, 0);
	W32(v, C_GF, (u32)v->features);
	W32(v, C_GFSEL, 1);
	W32(v, C_GF, (u32)(v->features >> 32));
	W8(v, C_STATUS, S_ACK | S_DRIVER | S_FEATURES_OK);
	if (!(R8(v, C_STATUS) & S_FEATURES_OK))
		return -E_IO;
	W16(v, C_MSIX, 0xFFFF); /* no config-change interrupt */
	return 0;
}

int virtio_setup_queue(struct virtio_dev *v, int index, u16 max_size, u16 msix_entry)
{
	W16(v, C_QSEL, (u16)index);
	u16 size = R16(v, C_QSIZE);
	if (!size)
		return -E_NOSYS;
	if (size > max_size)
		size = max_size;
	W16(v, C_QSIZE, size);
	/* One contiguous allocation: descriptors | avail | used (4 KiB aligned). */
	u64 dsz = 16ULL * size, asz = 6 + 2ULL * size, usz = 6 + 8ULL * size;
	u64 used_off = ALIGN_UP(dsz + asz, 4096);
	u64 total = used_off + ALIGN_UP(usz, 4096);
	paddr_t mem = pmm_alloc_frames(total / 4096);
	if (!mem)
		return -E_NOMEM;
	struct virtq *q = kzalloc(sizeof(*q));
	q->size = size;
	q->index = (u16)index;
	q->mem_phys = mem;
	q->desc = phys_to_virt(mem);
	q->avail = (u16 *)((u8 *)q->desc + dsz);
	q->used = (u8 *)q->desc + used_off;
	q->cookies = kzalloc(sizeof(void *) * size);
	for (u16 i = 0; i < size; i++)
		q->desc[i].next = (u16)(i + 1);
	q->free_head = 0;
	q->num_free = size;
	W64(v, C_QDESC, mem);
	W64(v, C_QDRIVER, mem + dsz);
	W64(v, C_QDEVICE, mem + used_off);
	W16(v, C_QMSIX, msix_entry);
	u16 noff = R16(v, C_QNOTIFY);
	q->notify = (volatile u16 *)(v->notify_base + (u64)noff * v->notify_mult);
	W16(v, C_QENABLE, 1);
	v->vq[index] = q;
	if (index >= v->nvq)
		v->nvq = index + 1;
	return 0;
}

void virtio_driver_ok(struct virtio_dev *v)
{
	W8(v, C_STATUS, S_ACK | S_DRIVER | S_FEATURES_OK | S_DRIVER_OK);
}

int virtq_add(struct virtq *q, const paddr_t *addrs, const u32 *lens, const u16 *flags, int n,
	      void *cookie)
{
	u64 f = irq_save();
	if (q->num_free < n) {
		irq_restore(f);
		return -E_AGAIN;
	}
	u16 head = q->free_head, idx = head;
	for (int i = 0; i < n; i++) {
		struct virtq_desc *d = &q->desc[idx];
		d->addr = addrs[i];
		d->len = lens[i];
		d->flags = flags[i] | (i + 1 < n ? VIRTQ_DESC_F_NEXT : 0);
		if (i + 1 < n)
			idx = d->next;
	}
	q->free_head = q->desc[idx].next;
	q->num_free = (u16)(q->num_free - n);
	q->cookies[head] = cookie;
	u16 aidx = q->avail[1];
	q->avail[2 + aidx % q->size] = head;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	q->avail[1] = (u16)(aidx + 1);
	irq_restore(f);
	return head;
}

void virtq_kick(struct virtq *q)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	*q->notify = q->index;
}

void *virtq_pop(struct virtq *q, u32 *len)
{
	u64 f = irq_save();
	volatile u16 *used_idx = (volatile u16 *)(q->used + 2);
	if (q->last_used == *used_idx) {
		irq_restore(f);
		return NULL;
	}
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	volatile u32 *elem = (volatile u32 *)(q->used + 4 + 8 * (q->last_used % q->size));
	u16 head = (u16)elem[0];
	if (len)
		*len = elem[1];
	q->last_used++;
	void *cookie = q->cookies[head];
	/* return the chain to the free list */
	u16 idx = head;
	u16 n = 1;
	while (q->desc[idx].flags & VIRTQ_DESC_F_NEXT) {
		idx = q->desc[idx].next;
		n++;
	}
	q->desc[idx].next = q->free_head;
	q->free_head = head;
	q->num_free = (u16)(q->num_free + n);
	irq_restore(f);
	return cookie;
}
