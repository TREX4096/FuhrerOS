# Kernel design

## Principles
- **Freestanding.** No host headers except the compiler's freestanding set
  (`stdint.h`, `stddef.h`, `stdarg.h`, `stdbool.h`). The kernel's own
  `lib/string.c` and `lib/printf.c` provide the C runtime; `lib/cxxrt.cpp`
  provides `operator new/delete`, `__cxa_pure_virtual` (no exceptions/RTTI).
- **Stages with tests.** Every boot stage has a self-test (docs/architecture.md).
- **Interrupts off = lock** on one CPU (D-105). Long operations use sleeping
  mutexes (VFS/FFS0, compositor structure changes).
- **Everything observable** through `/proc`.

## Entry and early boot (`core/main.c`)
`kmain` (the ELF entry, called by Limine on its own stack) initialises COM1,
checks the Limine base revision, copies the handoff into `struct boot_info`
(`core/boot.c`), then switches to a kernel-owned 64 KiB boot stack and runs
`kmain_stage2`, which brings up memory, descriptor tables, paging, heap,
ACPI/APIC and the timer, and finally `kernel_late_init` (scheduler +
`kinit` thread). After the kernel page tables and stack are active, all
bootloader-reclaimable memory is returned to the frame allocator.

## Interrupts (`arch/x86_64`)
- 256 assembly stubs push a uniform `struct trap_frame`; `isr_dispatch`
  routes exceptions (user → kill process, kernel → panic), IRQs and MSI-X
  vectors. After each interrupt `sched_irq_exit` may preempt and, when
  returning to ring 3, `proc_check_killed` terminates killed processes.
- Double fault and NMI use IST stacks.
- Legacy PICs are remapped and masked; the LAPIC (xAPIC MMIO) and IOAPIC are
  found through the ACPI MADT (including interrupt source overrides).
- The TSC and LAPIC timer are calibrated against PIT channel 2 (median of 3
  runs of 10 ms); `time_ns()` is TSC based. The scheduler tick is 1000 Hz.

## Panics
`FUHREROS KERNEL PANIC` with reason, CPU, RIP (symbolised), RSP, CR2, error
code, current task and process, registers and a frame-pointer stack trace.
Symbols come from a two-pass link (`scripts/gen-ksyms.sh`).

## System calls
`syscall` → `arch/x86_64/syscall.S` switches to the task's kernel stack via
the per-CPU area (GS base, `swapgs`), builds a `trap_frame` and calls
`syscall_dispatch`; interrupts are re-enabled inside syscalls (preemptible
kernel). Return is through `iretq`. See docs/syscalls.md.
