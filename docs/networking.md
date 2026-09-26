# Networking

Stages (§29), all implemented in `kernel/net/` and `drivers/virtio/net.c`:

1. **virtio-net** — receive queue with 64 posted 2 KiB buffers, transmit
   queue, one MSI-X vector.
2. **Ethernet, ARP** — 32-entry cache; frames waiting for resolution are
   queued (up to 8) and sent when the reply arrives.
3. **IPv4** — header checksums, routing via the netmask/gateway, local
   (loopback) delivery for our own address and 127/8; no fragmentation.
4. **ICMP** — echo replies; echo requests from user space through
   `SOCK_ICMP` sockets (`ping`).
5. **UDP** and **DHCP** — the DHCP client configures eth0 at boot (QEMU
   user networking: 10.0.2.15/24, gateway .2, DNS .3; static fallback).
6. **TCP** (D-114) — handshakes, sliding window, go-back-N with exponential
   RTO, fast retransmit (3 dup ACKs), zero-window probes, window updates,
   FIN/RST, TIME_WAIT (1 s).
7. **Sockets** — file descriptors (`VT_SOCK`), so read/write/close/poll work.
8. **DNS** resolver (user space, `user/lib/net.c`) and an **HTTP client**
   (`fetch`), an HTTP server (`httpd`, host port 8080 → guest 80) and `nc`
   (echo server on host port 7777 → guest 7).

Receive processing and protocol timers run in the `netd` kernel thread;
frames are copied into a 256-slot ring in interrupt context.

Tested by `nettest`: interface up via DHCP; 20 000 bytes echoed through the
kernel's own TCP stack; DNS resolution of example.com; `fetch
http://example.com/` returns "Example Domain".

**Bugs found by the scheduler experiments** (research-log/failures): a
zero-window deadlock (F-107) and a sender that ignored the zero window
without fast retransmit (F-108) — invisible under round-robin, exposed when
the adaptive policy delayed the consumer.

Limitations: no IPv6, no TLS (https), no congestion control, no IP
fragmentation, one interface.
