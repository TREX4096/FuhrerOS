/* virtio-net driver: queue 0 = receive, queue 1 = transmit. */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "kernel.h"
#include "mm.h"
#include "net.h"
#include "virtio.h"

#define VIRTIO_NET_F_MAC (1ULL << 5)
#define NET_HDR 12 /* struct virtio_net_hdr with VERSION_1 */
#define RXBUF 2048
#define RX_N 64
#define TX_N 64

struct vnet {
	struct virtio_dev v;
	struct netif nif;
	paddr_t rx_phys;	/* RX_N * 2 KiB */
	u8 *rx_virt;
	paddr_t tx_phys;
	u8 *tx_virt;
	u32 tx_next;
};

static void post_rx(struct vnet *n, u32 i)
{
	paddr_t a = n->rx_phys + (u64)i * RXBUF;
	u32 len = RXBUF;
	u16 fl = VIRTQ_DESC_F_WRITE;
	virtq_add(n->v.vq[0], &a, &len, &fl, 1, (void *)(uintptr_t)(i + 1));
}

static void vnet_irq(struct trap_frame *tf, void *ctx)
{
	struct vnet *n = ctx;
	void *cookie;
	u32 len;
	bool reposted = false;
	while ((cookie = virtq_pop(n->v.vq[0], &len))) {
		u32 i = (u32)(uintptr_t)cookie - 1;
		if (len > NET_HDR)
			net_rx(&n->nif, n->rx_virt + (u64)i * RXBUF + NET_HDR, len - NET_HDR);
		post_rx(n, i);
		reposted = true;
	}
	if (reposted)
		virtq_kick(n->v.vq[0]);
	while (virtq_pop(n->v.vq[1], &len))
		; /* transmit completions: buffers are reused round-robin */
}

static int vnet_transmit(struct netif *nif, const void *frame, u32 len)
{
	struct vnet *n = nif->priv;
	if (len > RXBUF - NET_HDR)
		return -E_INVAL;
	u64 f = irq_save();
	/* reclaim finished transmissions first */
	u32 dummy;
	while (virtq_pop(n->v.vq[1], &dummy))
		;
	u32 slot = n->tx_next++ % TX_N;
	u8 *buf = n->tx_virt + (u64)slot * RXBUF;
	memset(buf, 0, NET_HDR);
	memcpy(buf + NET_HDR, frame, len);
	paddr_t a = n->tx_phys + (u64)slot * RXBUF;
	u32 l = len + NET_HDR;
	u16 fl = 0;
	int r = virtq_add(n->v.vq[1], &a, &l, &fl, 1, (void *)1);
	if (r >= 0)
		virtq_kick(n->v.vq[1]);
	irq_restore(f);
	if (r < 0) {
		nif->rx_dropped++;
		return r;
	}
	nif->tx_packets++;
	nif->tx_bytes += len;
	return 0;
}

int virtio_net_probe(struct pci_dev *d)
{
	struct vnet *n = kzalloc(sizeof(*n));
	int r = virtio_init(&n->v, d, VIRTIO_NET_F_MAC);
	if (r < 0) {
		KLOG("vnet", "init failed (%d)", r);
		return r;
	}
	int vec = irq_alloc_vector();
	if (vec < 0)
		return -E_NOSYS;
	/* both queues share one MSI-X vector */
	pci_msix_set(d, 0, (u8)vec);
	irq_register((u8)vec, vnet_irq, n);
	pci_msix_enable(d);
	if (virtio_setup_queue(&n->v, 0, RX_N, 0) < 0 || virtio_setup_queue(&n->v, 1, TX_N, 0) < 0)
		return -E_IO;
	n->rx_phys = pmm_alloc_frames(RX_N * RXBUF / 4096);
	n->tx_phys = pmm_alloc_frames(TX_N * RXBUF / 4096);
	n->rx_virt = phys_to_virt(n->rx_phys);
	n->tx_virt = phys_to_virt(n->tx_phys);
	if (n->v.features & VIRTIO_NET_F_MAC)
		for (int i = 0; i < 6; i++)
			n->nif.mac[i] = n->v.devcfg[i];
	virtio_driver_ok(&n->v);
	for (u32 i = 0; i < RX_N; i++)
		post_rx(n, i);
	virtq_kick(n->v.vq[0]);
	strlcpy(n->nif.name, "eth0", sizeof(n->nif.name));
	n->nif.transmit = vnet_transmit;
	n->nif.priv = n;
	net_register_netif(&n->nif);
	KLOG("vnet", "eth0 MAC %02x:%02x:%02x:%02x:%02x:%02x", n->nif.mac[0], n->nif.mac[1],
	     n->nif.mac[2], n->nif.mac[3], n->nif.mac[4], n->nif.mac[5]);
	return 0;
}
