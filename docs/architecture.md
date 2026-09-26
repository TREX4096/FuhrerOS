# FuhrerOS Architecture

FuhrerOS is an x86-64 operating system with **its own kernel**, written from
scratch in C, a little C++ and assembly. Limine loads it; nothing of Linux
runs inside the VM (verified on every test boot).

```text
Limine (BIOS/UEFI)                         boot/limine.conf
    │ loads fuhreros.elf + initrd.tar
    ▼
FuhrerOS kernel ─────────────────────────────────────────────── kernel/
 ├─ arch/x86_64   GDT/TSS, IDT, ISR/syscall entry, LAPIC/IOAPIC,
 │                ACPI MADT, TSC/LAPIC timer, context switch
 ├─ mm            bitmap PMM, 4-level page tables (VMM), slab heap
 ├─ sched         task core (C) + policies (C++): round_robin,
 │                priority, low_latency, adaptive; workload profiler
 ├─ proc          processes, ELF64 loader, syscalls, pipes, ports
 ├─ fs            VFS, FFS0, tarfs (initrd), ramfs, devfs, procfs,
 │                buffer cache (5 policies)
 ├─ drivers       PCI, virtio (blk, net), PS/2 kbd+mouse, input +
 │                gesture recognizer, console TTY
 ├─ net           Ethernet, ARP, IPv4, ICMP, UDP, DHCP, TCP, sockets
 └─ gfx           framebuffer, text console, compositor + window manager
    ▲ syscall ABI (include/uapi/fuhrer.h)
    │
User space (ring 3) ─────────────────────────────────────────── user/
 ├─ libfu         syscalls, heap, stdio, strings, DNS, GUI toolkit
 ├─ init, sh      session, shell (pipes, redirection, history)
 ├─ bin/*         ls cat cp mv rm mkdir ps top mem cpu sched …
 │                ifconfig ping dns fetch httpd nc …
 │                schedbench iobench transition cachectl (research)
 └─ apps/*        desktop term files control monitor web edit settings
```

## Boot sequence (NEW_EXPLANATION §9) and its tests

| stage | code | self-test (`scripts/test.sh`) |
|---|---|---|
| entry, serial | `core/main.c`, `arch/x86_64/serial.c` | banner on COM1 |
| CPU checks | `cpu_sanity()` | `cpu.features` |
| boot info | `core/boot.c` | `boot.memmap`, `boot.framebuffer` |
| PMM | `mm/pmm.c` | `pmm.alloc_free`, `pmm.contiguous` |
| GDT, IDT, exceptions | `arch/x86_64/gdt.c`, `idt.c`, `isr.S` | `idt.exception.int3` |
| paging | `mm/vmm.c` | `vmm.map_translate_unmap`, `vmm.kernel_sections` |
| heap | `mm/heap.c` | `heap.*` (latency, fragmentation) |
| APIC, timers | `arch/x86_64/apic.c`, `acpi.c` | `apic.timer.ticks`, `time.tsc.monotonic` |
| scheduler | `sched/` | `sched.concurrent.*`, `sched.sleep_accuracy`, `sync.*`, `profiler.*` |
| devices, fs | `core/subsys.c` | late tests: `gesture.*`, `bcache.policies` |
| processes, syscalls, user space | `proc/`, `user/` | `user.suite` = 20+ `UTEST` + `NTEST` checks |

## Address space layout

```text
0x0000_0000_0040_0000   user program (ELF, static)
0x0000_2000_0000_0000   user mmap region (heap growth, thread stacks)
0x0000_3000_0000_0000   window surfaces (64 MiB per window slot)
0x0000_7FFF_F000_0000   argv block
0x0000_7FFF_FFFF_E000   top of user stack (256 KiB)
---------------------------------------------------------------- kernel half
HHDM (Limine offset)    all RAM, 2 MiB pages; framebuffer write-combining
0xFFFF_C000_0000_0000   kernel heap / vmalloc
0xFFFF_C800_0000_0000   kernel stacks (guard page below each)
0xFFFF_D000_0000_0000   MMIO (uncached)
0xFFFF_FFFF_8000_0000   kernel image (text RX, rodata R, data RW+NX)
```

All 256 upper PML4 entries are pre-allocated, so every process shares the
kernel half by copying the top-level entries.

## Key properties

- **Own kernel** — no Linux code or headers; freestanding toolchain;
  `check-kernel.sh` guards the ELF; tests assert no Linux banner.
- **Privilege boundary** — ring 3 processes, `syscall`/`iretq`, TSS RSP0;
  a user fault terminates only that process (tested).
- **Research instrumentation** — `/proc/sched`, `/proc/tasks`, `/proc/adapt`,
  `/proc/bcache`, `/proc/net`, `/proc/desktop`; the Control Center shows them
  live.
- **Uniprocessor** (D-105), **no FPU state** (D-104) — both documented
  limitations.
