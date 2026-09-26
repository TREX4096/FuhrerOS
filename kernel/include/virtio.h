/* virtio 1.x over PCI (modern transport), split virtqueues. */
#ifndef FUHRER_VIRTIO_H
#define FUHRER_VIRTIO_H
#include "pci.h"
#include "types.h"

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

struct virtq_desc {
	u64 addr;
	u32 len;
	u16 flags;
	u16 next;
} PACKED;

struct virtq {
	u16 size;
	u16 index;
	struct virtq_desc *desc;
	volatile u16 *avail;		/* flags, idx, ring[size], used_event */
	volatile u8 *used;		/* flags, idx, ring[size] {u32 id, u32 len} */
	u16 free_head;
	u16 num_free;
	u16 last_used;
	void **cookies;			/* per head descriptor */
	volatile u16 *notify;
	paddr_t mem_phys;
};

struct virtio_dev {
	struct pci_dev *pci;
	volatile u8 *common;
	volatile u8 *isr;
	volatile u8 *devcfg;
	u8 *notify_base;
	u32 notify_mult;
	u64 features;
	struct virtq *vq[4];
	int nvq;
	u8 vector;
};

/* Negotiate `wanted` (device-specific) feature bits plus VERSION_1. */
int virtio_init(struct virtio_dev *v, struct pci_dev *d, u64 wanted);
int virtio_setup_queue(struct virtio_dev *v, int index, u16 max_size, u16 msix_entry);
void virtio_driver_ok(struct virtio_dev *v);
/* Add a buffer chain; returns head index or -E_AGAIN if the ring is full. */
int virtq_add(struct virtq *q, const paddr_t *addrs, const u32 *lens, const u16 *flags, int n,
	      void *cookie);
void virtq_kick(struct virtq *q);
/* Pop one completed chain; returns cookie (NULL if none) and length. */
void *virtq_pop(struct virtq *q, u32 *len);
#endif
